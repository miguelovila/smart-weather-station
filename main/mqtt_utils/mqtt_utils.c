#include "mqtt_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "mqtt_utils";

// MQTT client handle
static esp_mqtt_client_handle_t mqtt_client = NULL;

// Connection status
static bool is_connected = false;

// Event group for synchronization
static EventGroupHandle_t mqtt_event_group = NULL;
static const int MQTT_CONNECTED_BIT = BIT0;
static const int MQTT_DISCONNECTED_BIT = BIT1;

// Connection timeout in milliseconds
static const uint32_t MQTT_CONNECT_TIMEOUT_MS = 10000;

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            is_connected = true;
            if (mqtt_event_group) {
                xEventGroupSetBits(mqtt_event_group, MQTT_CONNECTED_BIT);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            is_connected = false;
            if (mqtt_event_group) {
                xEventGroupSetBits(mqtt_event_group, MQTT_DISCONNECTED_BIT);
            }
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            is_connected = false;
            if (mqtt_event_group) {
                xEventGroupSetBits(mqtt_event_group, MQTT_DISCONNECTED_BIT);
            }
            break;
            
        default:
            ESP_LOGD(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

mqtt_error_t mqtt_utils_connect(const mqtt_config_t* config)
{
    if (config == NULL || config->broker_url == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return MQTT_ERROR_INVALID_CONFIG;
    }

    // If already connected, disconnect first
    if (mqtt_client != NULL) {
        mqtt_utils_disconnect();
    }

    if (mqtt_event_group == NULL) {
        mqtt_event_group = xEventGroupCreate();
        if (mqtt_event_group == NULL) {
            ESP_LOGE(TAG, "Failed to create event group");
            return MQTT_ERROR_INIT_FAILED;
        }
    }

    xEventGroupClearBits(mqtt_event_group, MQTT_CONNECTED_BIT | MQTT_DISCONNECTED_BIT);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = config->broker_url,
    };
    if (config->client_id != NULL) mqtt_cfg.credentials.client_id = config->client_id;
    if (config->username != NULL) mqtt_cfg.credentials.username = config->username;
    if (config->password != NULL) mqtt_cfg.credentials.authentication.password = config->password;

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return MQTT_ERROR_INIT_FAILED;
    }

    esp_err_t err = esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return MQTT_ERROR_INIT_FAILED;
    }

    err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return MQTT_ERROR_CONNECTION_FAILED;
    }

    // Wait for connection or timeout
    EventBits_t bits = xEventGroupWaitBits(
        mqtt_event_group,
        MQTT_CONNECTED_BIT | MQTT_DISCONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(MQTT_CONNECT_TIMEOUT_MS)
    );

    if (bits & MQTT_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Successfully connected to MQTT broker");
        return MQTT_OK;
    } else {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker (timeout or error)");
        // Clean up
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        is_connected = false;
        return MQTT_ERROR_CONNECTION_FAILED;
    }
}

mqtt_error_t mqtt_utils_disconnect(void)
{
    if (mqtt_client == NULL) {
        ESP_LOGW(TAG, "MQTT client not initialized");
        return MQTT_OK;
    }

    // Clear event bits
    if (mqtt_event_group) {
        xEventGroupClearBits(mqtt_event_group, MQTT_CONNECTED_BIT | MQTT_DISCONNECTED_BIT);
    }

    // Stop MQTT client
    esp_err_t err = esp_mqtt_client_stop(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(err));
    }

    // Wait for disconnection
    if (mqtt_event_group && is_connected) {
        EventBits_t bits = xEventGroupWaitBits(
            mqtt_event_group,
            MQTT_DISCONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(5000) // 5 second timeout
        );

        if (!(bits & MQTT_DISCONNECTED_BIT)) {
            ESP_LOGW(TAG, "Disconnect timeout, forcing cleanup");
        }
    }

    // Destroy MQTT client
    esp_mqtt_client_destroy(mqtt_client);
    mqtt_client = NULL;
    is_connected = false;

    // Clean up event group
    if (mqtt_event_group) {
        vEventGroupDelete(mqtt_event_group);
        mqtt_event_group = NULL;
    }

    ESP_LOGI(TAG, "MQTT client disconnected and cleaned up");
    return MQTT_OK;
}

mqtt_error_t mqtt_utils_publish(const char* topic, const char* message)
{
    if (topic == NULL || message == NULL) {
        ESP_LOGE(TAG, "Invalid parameters: topic or message is NULL");
        return MQTT_ERROR_INVALID_CONFIG;
    }

    if (mqtt_client == NULL || !is_connected) {
        ESP_LOGE(TAG, "MQTT client not connected");
        return MQTT_ERROR_NOT_CONNECTED;
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, message, 0, 0, 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish message to topic: %s", topic);
        return MQTT_ERROR_PUBLISH_FAILED;
    }

    ESP_LOGI(TAG, "Published message to topic '%s', msg_id=%d", topic, msg_id);
    return MQTT_OK;
}

bool mqtt_utils_is_connected(void)
{
    return is_connected && (mqtt_client != NULL);
}
