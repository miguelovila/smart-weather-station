#include "data_handler.h"

const char *TAG = "data_handler";

#define SD_CARD_ERROR_CHECK(ret) \
    do { \
        if (ret != SD_OK) { \
            ESP_LOGE(TAG, "SD card error: %d", ret); \
        } else { \
            ESP_LOGI(TAG, "Saved BME280 data to SD card: %s", message); \
        } \
    } while (0)

#define MQTT_ERROR_CHECK(ret) \
    do { \
        if (ret != MQTT_OK) { \
            ESP_LOGE(TAG, "MQTT error: %d", ret); \
        } else { \
            ESP_LOGI(TAG, "Published BME280 data to MQTT broker: %s", message); \
        } \
    } while (0)

void handle_bme280_data(bme280_data_t *data) {
    if (data == NULL) return;
    char *message;
    time_t timestamp = time(NULL);

    // Save on SD Card
    asprintf(&message, "%lld,%f,%f,%f\n",
        timestamp, data->temperature, data->pressure, data->humidity);
    sd_error_t sd_ret;
    sd_ret = sd_append_text_file("bme280_data.csv", message);
    SD_CARD_ERROR_CHECK(sd_ret);

    // Publish to MQTT
    asprintf(&message, "{\"timestamp\": %lld,\"temperature\":%f,\"pressure\":%f,\"humidity\":%f}",
        timestamp, data->temperature, data->pressure, data->humidity);
    mqtt_error_t mqtt_ret;
    mqtt_ret = mqtt_utils_publish("ase-proj/bme280", message);
    MQTT_ERROR_CHECK(mqtt_ret);

    free(message);
}

char* wind_direction_string(wind_direction_t direction)
{
    switch (direction) {
        case WIND_DIRECTION_NORTH: return "North";
        case WIND_DIRECTION_NORTH_EAST: return "North-East";
        case WIND_DIRECTION_EAST: return "East";
        case WIND_DIRECTION_SOUTH_EAST: return "South-East";
        case WIND_DIRECTION_SOUTH: return "South";
        case WIND_DIRECTION_SOUTH_WEST: return "South-West";
        case WIND_DIRECTION_WEST: return "West";
        case WIND_DIRECTION_NORTH_WEST: return "North-West";
        default: return "Unknown";
    }
}

void handle_wva420_data(wind_direction_t *data) {
    if (data == NULL) return;
    char *message;
    time_t timestamp = time(NULL);

    // Save on SD Card
    asprintf(&message, "%lld,%s\n",
        timestamp, wind_direction_string(*data));
    sd_error_t sd_ret;
    sd_ret = sd_append_text_file("wva420_data.csv", message);
    SD_CARD_ERROR_CHECK(sd_ret);

    // Publish to MQTT
    asprintf(&message, "{\"timestamp\": %lld,\"wind_direction\":\"%s\"}",
        timestamp, wind_direction_string(*data));
    mqtt_error_t mqtt_ret;
    mqtt_ret = mqtt_utils_publish("ase-proj/wva420", message);
    MQTT_ERROR_CHECK(mqtt_ret);

    free(message);
}

void handle_wsp420_data(float speed) {
    char *message;
    time_t timestamp = time(NULL);

    // Save on SD Card
    asprintf(&message, "%lld,%f\n",
        timestamp, speed);
    sd_error_t sd_ret;
    sd_ret = sd_append_text_file("wsp420_data.csv", message);
    SD_CARD_ERROR_CHECK(sd_ret);

    // Publish to MQTT
    asprintf(&message, "{\"timestamp\": %lld,\"wind_speed\":\"%f\"}",
        timestamp, speed);
    mqtt_error_t mqtt_ret;
    mqtt_ret = mqtt_utils_publish("ase-proj/wsp420", message);
    MQTT_ERROR_CHECK(mqtt_ret);

    free(message);
}
