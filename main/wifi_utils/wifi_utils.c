#include "wifi_utils.h"

static const char *TAG = "wifi_utils";

// Event group for WiFi events
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// Internal state
static wifi_utils_config_t internal_config = {0};
static wifi_status_t current_status = WIFI_STATUS_DISCONNECTED;
static bool initialized = false;
static int retry_count = 0;
static esp_netif_t *netif_instance = NULL;
static esp_netif_ip_info_t ip_info;
static esp_event_handler_instance_t instance_any_id = NULL;
static esp_event_handler_instance_t instance_got_ip = NULL;

/**
 * @brief WiFi event handler
 * 
 * Handles WiFi connection events and implements retry logic.
 */
static void wifi_utils_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_count < internal_config.max_retry_attempts || internal_config.max_retry_attempts == 0) {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGI(TAG, "Retrying connection...");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        current_status = WIFI_STATUS_CONNECTED;
        retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

wifi_error_t wifi_utils_init()
{
    if (initialized) {
        ESP_LOGW(TAG, "WiFi already initialized");
        return WIFI_OK;
    }

    // Create event group
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    netif_instance = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                     &wifi_utils_event_handler, NULL,
                                     &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     &wifi_utils_event_handler, NULL,
                                     &instance_got_ip));

    initialized = true;
    current_status = WIFI_STATUS_DISCONNECTED;
    retry_count = 0;

    ESP_LOGI(TAG, "WiFi initialized successfully");
    return WIFI_OK;
}

wifi_error_t wifi_utils_connect(const wifi_utils_config_t *config)
{
    if (!initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return WIFI_ERROR_NOT_INITIALIZED;
    }

    if (config == NULL || strlen(config->ssid) == 0 || strlen(config->password) == 0) {
        ESP_LOGE(TAG, "Invalid WiFi configuration");
        return WIFI_ERROR_INVALID_CONFIG;
    }

    // Copy configuration to internal state
    memcpy(&internal_config, config, sizeof(wifi_utils_config_t));

    // Set WiFi configuration
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = ""
        }
    };
    strncpy((char *)wifi_config.sta.ssid, internal_config.ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, internal_config.password, sizeof(wifi_config.sta.password) - 1);

    // Start WiFi station mode
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return WIFI_ERROR_INIT_FAILED;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        return WIFI_ERROR_INIT_FAILED;
    }

    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return WIFI_ERROR_INIT_FAILED;
    }

    // Reset retry count and status
    retry_count = 0;
    current_status = WIFI_STATUS_CONNECTING;

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", internal_config.ssid);

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(internal_config.retry_interval_ms * (internal_config.max_retry_attempts > 0 ? internal_config.max_retry_attempts : 10)));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID: %s", internal_config.ssid);
        current_status = WIFI_STATUS_CONNECTED;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi SSID: %s", internal_config.ssid);
        current_status = WIFI_STATUS_FAILED;
        return WIFI_ERROR_CONNECTION_FAILED;
    } else {
        ESP_LOGE(TAG, "WiFi connection timed out");
        current_status = WIFI_STATUS_FAILED;
        return WIFI_ERROR_CONNECTION_FAILED;
    }
    return WIFI_OK;
}

wifi_error_t wifi_utils_disconnect()
{
    if (!initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return WIFI_ERROR_NOT_INITIALIZED;
    }

    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect WiFi: %s", esp_err_to_name(ret));
        return WIFI_ERROR_CONNECTION_FAILED;
    }

    current_status = WIFI_STATUS_DISCONNECTED;
    retry_count = 0;

    ESP_LOGI(TAG, "Disconnected from WiFi");
    return WIFI_OK;
}

wifi_status_t wifi_utils_get_status()
{
    return current_status;
}

bool wifi_utils_is_connected()
{
    return current_status == WIFI_STATUS_CONNECTED;
}

wifi_error_t wifi_utils_get_ip_addr(char *ip_addr)
{
    if (!initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return WIFI_ERROR_NOT_INITIALIZED;
    }

    if (ip_addr == NULL) {
        ESP_LOGE(TAG, "Invalid IP address buffer");
        return WIFI_ERROR_INVALID_CONFIG;
    }

    // Get IP address
    esp_err_t ret = esp_netif_get_ip_info(netif_instance, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP address: %s", esp_err_to_name(ret));
        return WIFI_ERROR_CONNECTION_FAILED;
    }

    snprintf(ip_addr, 16, IPSTR, IP2STR(&ip_info.ip));
    return WIFI_OK;
}

wifi_error_t wifi_utils_deinit()
{
    if (!initialized) {
        ESP_LOGW(TAG, "WiFi not initialized");
        return WIFI_ERROR_NOT_INITIALIZED;
    }
    
    // Unregister event handlers
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_utils_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_utils_event_handler);

    // Stop WiFi
    esp_wifi_stop();
    esp_wifi_deinit();

    initialized = false;
    current_status = WIFI_STATUS_DISCONNECTED;

    ESP_LOGI(TAG, "WiFi deinitialized successfully");
    return WIFI_OK;
}
