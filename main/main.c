#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_log.h"

#include "bme280/bme280.h"
#include "sd_utils/sd_utils.h"
#include "wifi_utils/wifi_utils.h"
#include "mqtt_utils/mqtt_utils.h"
#include "wind_direction/wind_direction_sensor.h"
#include "wind_speed/wind_speed_sensor.h"

#include "data_handler/data_handler.h"

static const char *TAG = "WEATHER STATION";

// Connectivity Defaults
static char WIFI_SSID[32];
static char WIFI_PASSWORD[32];
static char MQTT_CLIENT_ID[64];
static char MQTT_BROKER_URL[128];

#define DEFAULT_WIFI_SSID "Vacalo IoT"
#define DEFAULT_WIFI_PASSWORD "CVCV0011223344VCVC"
#define DEFAULT_MQTT_BROKER_URL "mqtt://public-mqtt.franciscoribeiro.pt:1883"
#define DEFAULT_MQTT_CLIENT_ID "weather_station_001"

// Devices
typedef struct bme280_task_args_t {
    bme280_sensor_t *sensor;
    bme280_data_t *data;
} bme280_task_args_t;
static bme280_sensor_t bme280_sensor;
static bme280_data_t bme280_data;
static bme280_task_args_t bme280_task_args;

// SD Card Pins
#define SD_CARD_MOUNT_POINT "/sdcard"
#define SD_CARD_MISO_PIN 10
#define SD_CARD_MAX_FILES 5
#define SD_CARD_MOSI_PIN 1
#define SD_CARD_CLK_PIN 9
#define SD_CARD_CS_PIN 0

// Anemometer Sensor Pin
#define WIND_SPEED_SENSOR_PIN 8
#define WIND_SPEED_SENSOR_RADIUS_CM 4

// Wind Vane Sensor Pins
#define WIND_DIRECTION_SENSOR_YELLOW_PIN 7
#define WIND_DIRECTION_SENSOR_GREEN_PIN 5
#define WIND_DIRECTION_SENSOR_BROWN_PIN 6
#define WIND_DIRECTION_SENSOR_BLUE_PIN 4

// I2C Bus Pins
#define I2C_SDA_PIN 3
#define I2C_SCL_PIN 2

#define BME280_I2C_ADDR BME280_I2C_ADDR_PRIMARY

// ======================================================
// =================== INIT FUNCTIONS ===================
// ======================================================

void start_wifi()
{
    wifi_error_t wifi_ret;
    ESP_LOGI(TAG, "Starting WiFi connection...");

    wifi_ret = wifi_utils_init();
    if (wifi_ret != WIFI_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed: %d", wifi_ret);
        return;
    }

    wifi_utils_config_t wifi_config = {
        .retry_interval_ms = 5000, // Retry every 5 seconds
        .max_retry_attempts = 0,   // Infinite retries
    };

    strncpy(wifi_config.ssid, WIFI_SSID, sizeof(wifi_config.ssid) - 1);
    strncpy(wifi_config.password, WIFI_PASSWORD, sizeof(wifi_config.password) - 1);
    wifi_config.ssid[sizeof(wifi_config.ssid) - 1] = '\0';
    wifi_config.password[sizeof(wifi_config.password) - 1] = '\0';

    wifi_ret = wifi_utils_connect(&wifi_config);
    if (wifi_ret != WIFI_OK) {
        ESP_LOGE(TAG, "WiFi connection failed: %d", wifi_ret);
        return;
    }

    ESP_LOGI(TAG, "WiFi connected successfully");
    char ip_addr[16] = {0}; // Buffer for IP address
    if (wifi_utils_get_ip_addr(ip_addr) == WIFI_OK) {
        ESP_LOGI(TAG, "IP Address: %s", ip_addr);
    } else {
        ESP_LOGE(TAG, "Failed to get IP address");
    }
}

void connect_mqtt()
{
    mqtt_error_t mqtt_ret = mqtt_utils_connect(&(mqtt_config_t){
        .broker_url = MQTT_BROKER_URL,
        .client_id = MQTT_CLIENT_ID,
    });
    if (mqtt_ret != MQTT_OK) {
        ESP_LOGE(TAG, "MQTT connection failed: %d", mqtt_ret);
        return;
    }

    ESP_LOGI(TAG, "MQTT connected successfully");
    char *message_to_publish;
    asprintf(&message_to_publish, "Weather Station has started!");
    mqtt_ret = mqtt_utils_publish("ase-proj/info", message_to_publish);
    if (mqtt_ret != MQTT_OK) {
        ESP_LOGE(TAG, "Failed to publish MQTT message: %d", mqtt_ret);
    }
}

void init_sd()
{
    sd_error_t sd_ret = sd_init(&(sd_config_t){
        .miso_pin = SD_CARD_MISO_PIN,
        .mosi_pin = SD_CARD_MOSI_PIN,
        .clk_pin = SD_CARD_CLK_PIN,
        .cs_pin = SD_CARD_CS_PIN,
        .mount_point = SD_CARD_MOUNT_POINT,
        .max_files = SD_CARD_MAX_FILES,
    });
    if (sd_ret != SD_OK) {
        ESP_LOGE(TAG, "SD card initialization failed: %d", sd_ret);
        return;
    }
    ESP_LOGI(TAG, "SD card initialized successfully");
}

void init_bme280(bme280_sensor_t *bme280_sensor)
{
    esp_err_t ret;
    ret = bme280_init_i2c(bme280_sensor, BME280_I2C_ADDR, I2C_SDA_PIN, I2C_SCL_PIN, BME280_SCL_DEFAULT_FREQ_HZ);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BME280 initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    bme280_config_t bme280_config = {
        .temp_oversampling = BME280_OVERSAMPLING_1X,
        .press_oversampling = BME280_OVERSAMPLING_1X,
        .hum_oversampling = BME280_OVERSAMPLING_1X,
        .filter = BME280_FILTER_OFF,
        .standby_time = BME280_STANDBY_500_MS,
    };
    ret = bme280_configure(bme280_sensor, &bme280_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BME280 configuration failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = bme280_set_mode(bme280_sensor, BME280_MODE_NORMAL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set BME280 mode: %s", esp_err_to_name(ret));
        return;
    }
}

void init_wva420()
{
    wind_direction_config_t wind_direction_config = {
        .gpio_pin_north = WIND_DIRECTION_SENSOR_YELLOW_PIN,
        .gpio_pin_south = WIND_DIRECTION_SENSOR_GREEN_PIN,
        .gpio_pin_east = WIND_DIRECTION_SENSOR_BLUE_PIN,
        .gpio_pin_west = WIND_DIRECTION_SENSOR_BROWN_PIN
    };

    if (wind_direction_sensor_init(&wind_direction_config) != WIND_DIRECTION_OK) {
        ESP_LOGE(TAG, "WVA420 wind direction sensor initialization failed");
        return;
    }
}

void init_wsp420()
{
    wind_speed_config_t wind_speed_config = {
        .gpio_pin = WIND_SPEED_SENSOR_PIN,
        .radius_cm = WIND_SPEED_SENSOR_RADIUS_CM,
        .calibration_factor = 24.0 // Example calibration factor
    };

    if (wind_speed_sensor_init(&wind_speed_config) != WIND_SPEED_OK) {
        ESP_LOGE(TAG, "WSP420 wind speed sensor initialization failed");
        return;
    }
}

void sync_clock()
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update system time within 10s, timestamps will be wrong!");
    }
    ESP_LOGI(TAG, "Current timestamp: %lld", time(NULL));
}

// ======================================================
// =================== TASK FUNCTIONS ===================
// ======================================================

void task_get_bme_data(void *arg)
{
    while (1)
    {
        bme280_task_args_t *bme280_task_args = (bme280_task_args_t *) arg;

        esp_err_t ret = bme280_read_data(bme280_task_args->sensor, bme280_task_args->data);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read BME280 data: %s", esp_err_to_name(ret));
        } else {
            handle_bme280_data(bme280_task_args->data);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void task_get_wva_data(void *arg)
{
    while (1)
    {
        wind_direction_t direction;
        wind_direction_error_t err = wind_direction_get_reading(&direction);
        if (err != WIND_DIRECTION_OK) {
            ESP_LOGE(TAG, "Failed to read WVA420 data.");
        } else {
            handle_wva420_data(&direction);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void task_get_wsp_data(void *arg)
{
    while (1)
    {
        float speed;
        wind_speed_error_t err = wind_speed_get_reading(&speed);
        if (err != WIND_SPEED_OK) {
            ESP_LOGE(TAG, "Failed to read WSP420 data.");
        } else {
            handle_wsp420_data(speed);
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_sd();
    ESP_LOGI(TAG, "SD card initialized");
    if (!sd_file_exists("bme280_data.csv")) {
        ESP_LOGI(TAG, "Creating bme280_data.csv on SD card");
        sd_error_t sd_ret = sd_append_text_file("bme280_data.csv", "timestamp,temperature,pressure,humidity\n");
        if (sd_ret != SD_OK) {
            ESP_LOGE(TAG, "Failed to create bme280_data.csv: %d", sd_ret);
            return;
        }
    }
    if (!sd_file_exists("wva420_data.csv")) {
        ESP_LOGI(TAG, "Creating wva420_data.csv on SD card");
        sd_error_t sd_ret = sd_append_text_file("wva420_data.csv", "timestamp,wind_direction\n");
        if (sd_ret != SD_OK) {
            ESP_LOGE(TAG, "Failed to create wva420_data.csv: %d", sd_ret);
            return;
        }
    }
    if (!sd_file_exists("wsp420_data.csv")) {
        ESP_LOGI(TAG, "Creating wsp420_data.csv on SD card");
        sd_error_t sd_ret = sd_append_text_file("wsp420_data.csv", "timestamp,wind_speed\n");
        if (sd_ret != SD_OK) {
            ESP_LOGE(TAG, "Failed to create wsp420_data.csv: %d", sd_ret);
            return;
        }
    }

    sd_error_t sd_ret = sd_get_wifi_settings(WIFI_SSID, sizeof(WIFI_SSID), WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    if (sd_ret != SD_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi settings from SD card: %d", sd_ret);
        ESP_LOGI(TAG, "Using default WiFi settings");
        strncpy(WIFI_SSID, DEFAULT_WIFI_SSID, sizeof(WIFI_SSID) - 1);
        strncpy(WIFI_PASSWORD, DEFAULT_WIFI_PASSWORD, sizeof(WIFI_PASSWORD) - 1);
    } else {
        ESP_LOGI(TAG, "WiFi settings loaded from SD card: SSID: %s", WIFI_SSID);
    }

    sd_error_t sd_mqtt_ret = sd_get_mqtt_settings(MQTT_BROKER_URL, sizeof(MQTT_BROKER_URL),
                                                MQTT_CLIENT_ID, sizeof(MQTT_CLIENT_ID));
    if (sd_mqtt_ret != SD_OK) {
        ESP_LOGE(TAG, "Failed to get MQTT config from SD card: %d", sd_mqtt_ret);
        ESP_LOGI(TAG, "Using default MQTT config");
        strncpy(MQTT_BROKER_URL, DEFAULT_MQTT_BROKER_URL, sizeof(MQTT_BROKER_URL) - 1);
        strncpy(MQTT_CLIENT_ID, DEFAULT_MQTT_CLIENT_ID, sizeof(MQTT_CLIENT_ID) - 1);
    } else {
        ESP_LOGI(TAG, "MQTT settings loaded from SD card: Broker: %s", MQTT_BROKER_URL);
    }

    bme280_task_args.sensor = &bme280_sensor;
    bme280_task_args.data = &bme280_data;

    start_wifi();
    sync_clock();
    connect_mqtt();
    init_bme280(&bme280_sensor);
    init_wva420();
    init_wsp420();

    TaskHandle_t bme280_task;
    xTaskCreate(task_get_bme_data, "BME280_Task", 4096, &bme280_task_args, 10, &bme280_task);

    TaskHandle_t wva420_task;
    xTaskCreate(task_get_wva_data, "WVA420_Task", 4096, NULL, 10, &wva420_task);

    TaskHandle_t wsp420_task;
    xTaskCreate(task_get_wsp_data, "WSP420_Task", 4096, NULL, 10, &wsp420_task);
}
