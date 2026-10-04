#include "wind_speed_sensor.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "WIND_SPEED_SENSOR";

static wind_speed_config_t internal_config = {0};
static bool initialized = false;
static bool enabled = false;
static float rev_count = 0;

typedef struct isr_args_t {
    bool *enabled;
    float *rev_count;
} isr_args_t;
static isr_args_t isr_args;

static void IRAM_ATTR hall_sensor_isr_handler(void *arg)
{
    isr_args_t *isr_args = (isr_args_t *) arg;
    if (*isr_args->enabled == true) *isr_args->rev_count = *isr_args->rev_count + 1;
}

wind_speed_error_t wind_speed_sensor_init(const wind_speed_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return WIND_SPEED_ERROR;
    }

    if (config->gpio_pin < 0 || config->gpio_pin >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid GPIO pin: %d", config->gpio_pin);
        return WIND_SPEED_ERROR;
    }

    if (initialized) {
        ESP_LOGW(TAG, "Sensor already initialized");
        return WIND_SPEED_ERROR;
    }
    
    // Copy configuration
    memcpy(&internal_config, config, sizeof(wind_speed_config_t));
    
    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << internal_config.gpio_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO configuration failed: %s", esp_err_to_name(ret));
        return WIND_SPEED_ERROR;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
        return WIND_SPEED_ERROR;
    }

    isr_args.enabled = &enabled;
    isr_args.rev_count = &rev_count;
    ret = gpio_isr_handler_add(internal_config.gpio_pin, hall_sensor_isr_handler, &isr_args);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
        return WIND_SPEED_ERROR;
    }

    initialized = true;

    ESP_LOGI(TAG, "Wind speed sensor initialized on GPIO pin %d", config->gpio_pin);
    return WIND_SPEED_OK;
}

wind_speed_error_t wind_speed_sensor_deinit(void)
{
    if (!initialized) {
        return WIND_SPEED_ERROR;
    }
    
    // Reset GPIO
    gpio_reset_pin(internal_config.gpio_pin);
    
    // Clear config
    memset(&internal_config, 0, sizeof(wind_speed_config_t));

    initialized = false;

    ESP_LOGI(TAG, "Wind speed sensor deinitialized");
    return WIND_SPEED_OK;
}

wind_speed_error_t wind_speed_get_reading(float *speed)
{
    if (!initialized) {
        return WIND_SPEED_ERROR;
    }

    if (speed == NULL) {
        return WIND_SPEED_ERROR;
    }

    rev_count = 0;
    enabled = true;

    vTaskDelay(pdMS_TO_TICKS(7000));
    enabled = false;

    float ang_velocity = rev_count / 7;
    *speed = ang_velocity * (internal_config.radius_cm / 100) * internal_config.calibration_factor;
    
    return WIND_SPEED_OK;
}
