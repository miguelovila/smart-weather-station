#include "wind_direction_sensor.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "WIND_DIRECTION_SENSOR";

static wind_direction_config_t internal_config = {0};
static bool initialized = false;

wind_direction_error_t wind_direction_sensor_init(const wind_direction_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return WIND_DIRECTION_ERROR_INVALID_PARAM;
    }

    if (config->gpio_pin_north < 0 || config->gpio_pin_north >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid GPIO pin (North): %d", config->gpio_pin_north);
        return WIND_DIRECTION_ERROR_INVALID_PIN;
    }

    if (config->gpio_pin_east < 0 || config->gpio_pin_east >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid GPIO pin (East): %d", config->gpio_pin_east);
        return WIND_DIRECTION_ERROR_INVALID_PIN;
    }

    if (config->gpio_pin_south < 0 || config->gpio_pin_south >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid GPIO pin (South): %d", config->gpio_pin_south);
        return WIND_DIRECTION_ERROR_INVALID_PIN;
    }

    if (config->gpio_pin_west < 0 || config->gpio_pin_west >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid GPIO pin (West): %d", config->gpio_pin_west);
        return WIND_DIRECTION_ERROR_INVALID_PIN;
    }

    if (initialized) {
        ESP_LOGW(TAG, "Sensor already initialized");
        return WIND_DIRECTION_OK;
    }
    
    // Copy configuration
    memcpy(&internal_config, config, sizeof(wind_direction_config_t));
    
    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << internal_config.gpio_pin_north) | (1ULL << internal_config.gpio_pin_east) | (1ULL << internal_config.gpio_pin_south) | (1ULL << internal_config.gpio_pin_west),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO configuration failed: %s", esp_err_to_name(ret));
        return WIND_DIRECTION_ERROR_INIT_FAILED;
    }

    initialized = true;

    ESP_LOGI(TAG, "Wind direction sensor initialized on GPIO pins %d, %d, %d and %d", 
             config->gpio_pin_north, config->gpio_pin_east, config->gpio_pin_south, config->gpio_pin_west);
    return WIND_DIRECTION_OK;
}

wind_direction_error_t wind_direction_sensor_deinit(void)
{
    if (!initialized) {
        return WIND_DIRECTION_ERROR_NOT_INITIALIZED;
    }
    
    // Reset GPIO
    gpio_reset_pin(internal_config.gpio_pin_north);
    gpio_reset_pin(internal_config.gpio_pin_east);
    gpio_reset_pin(internal_config.gpio_pin_south);
    gpio_reset_pin(internal_config.gpio_pin_west);
    
    // Clear config
    memset(&internal_config, 0, sizeof(wind_direction_config_t));

    initialized = false;

    ESP_LOGI(TAG, "Wind direction sensor deinitialized");
    return WIND_DIRECTION_OK;
}

wind_direction_error_t wind_direction_get_reading(wind_direction_t *direction)
{
    if (!initialized) {
        return WIND_DIRECTION_ERROR_NOT_INITIALIZED;
    }

    if (direction == NULL) {
        return WIND_DIRECTION_ERROR_INVALID_PARAM;
    }
    
    // Get the state of each GPIO pin
    int north_state = 1 ^ gpio_get_level(internal_config.gpio_pin_north);
    int south_state = 1 ^ gpio_get_level(internal_config.gpio_pin_south);
    int east_state  = 1 ^ gpio_get_level(internal_config.gpio_pin_east);
    int west_state  = 1 ^ gpio_get_level(internal_config.gpio_pin_west);

    ESP_LOGI(TAG, "GPIO states - North: %d, East: %d, South: %d, West: %d", 
             north_state, east_state, south_state, west_state);

    // Determine wind direction based on GPIO states
    if (north_state) {
        if (east_state) {
            *direction = WIND_DIRECTION_NORTH_EAST;
        } else if (west_state) {
            *direction = WIND_DIRECTION_NORTH_WEST;
        } else {
            *direction = WIND_DIRECTION_NORTH;
        }
    } else if (east_state) {
        if (south_state) {
            *direction = WIND_DIRECTION_SOUTH_EAST;
        } else {
            *direction = WIND_DIRECTION_EAST;
        }
    } else if (south_state) {
        if (west_state) {
            *direction = WIND_DIRECTION_SOUTH_WEST;
        } else {
            *direction = WIND_DIRECTION_SOUTH;
        }
    } else if (west_state) {
        *direction = WIND_DIRECTION_WEST;
    } else {
        *direction = WIND_DIRECTION_UNKNOWN; // No direction detected
    }
    
    return WIND_DIRECTION_OK;
}
