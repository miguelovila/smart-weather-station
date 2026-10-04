#ifndef WIND_SPEED_SENSOR_H
#define WIND_SPEED_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Wind speed sensor configuration structure
 */
typedef struct {
    int gpio_pin;             // GPIO pin connected to pulse sensing
    float radius_cm;          // Radius of the anemometer in centimeters
    float calibration_factor; // Calibration factor for the sensor
} wind_speed_config_t;

/**
 * @brief Error codes for wind speed sensor operations
 */
typedef enum {
    WIND_SPEED_OK = 0,
    WIND_SPEED_ERROR = -1,
} wind_speed_error_t;


/**
 * @brief Initialize the wind speed sensor
 * 
 * Sets up the GPIO pin for the hall effect sensor and configures
 * interrupt handling for pulse counting.
 * 
 * @param config Pointer to sensor configuration structure
 * @return wind_speed_error_t Error code (WIND_SPEED_OK on success)
 */
wind_speed_error_t wind_speed_sensor_init(const wind_speed_config_t *config);

/**
 * @brief Deinitialize the wind speed sensor
 * 
 * Cleans up resources and disables interrupts.
 * 
 * @return wind_speed_error_t Error code (WIND_SPEED_OK on success)
 */
wind_speed_error_t wind_speed_sensor_deinit(void);

/**
 * @brief Get current wind speed reading
 * 
 * Calculates and returns the current wind speed.
 * 
 * @param speed Pointer to store wind speed in hm/h
 * @return wind_speed_error_t Error code (WIND_SPEED_OK on success)
 */
wind_speed_error_t wind_speed_get_reading(float *speed);

#endif // WIND_SPEED_SENSOR_H
