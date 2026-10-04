#ifndef WIND_DIRECTION_SENSOR_H
#define WIND_DIRECTION_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Wind direction sensor configuration structure
 */
typedef struct {
    int gpio_pin_north;                   // GPIO pin connected to north hall effect sensor
    int gpio_pin_east;                    // GPIO pin connected to east hall effect sensor
    int gpio_pin_south;                   // GPIO pin connected to south hall effect sensor
    int gpio_pin_west;                    // GPIO pin connected to west hall effect sensor
} wind_direction_config_t;

/**
 * @brief Error codes for wind direction sensor operations
 */
typedef enum {
    WIND_DIRECTION_OK = 0,
    WIND_DIRECTION_ERROR_INVALID_PIN = -1,
    WIND_DIRECTION_ERROR_INIT_FAILED = -2,
    WIND_DIRECTION_ERROR_NOT_INITIALIZED = -3,
    WIND_DIRECTION_ERROR_INVALID_PARAM = -4
} wind_direction_error_t;

/**
 * @brief Possible wind direction values
 */
typedef enum {
    WIND_DIRECTION_NORTH = 0,
    WIND_DIRECTION_NORTH_EAST = 45,
    WIND_DIRECTION_EAST = 90,
    WIND_DIRECTION_SOUTH_EAST = 135,
    WIND_DIRECTION_SOUTH = 180,
    WIND_DIRECTION_SOUTH_WEST = 225,
    WIND_DIRECTION_WEST = 270,
    WIND_DIRECTION_NORTH_WEST = 315,
    WIND_DIRECTION_UNKNOWN = -1
} wind_direction_t;

/**
 * @brief Initialize the wind direction sensor
 * 
 * Sets up the GPIO pin for the hall effect sensor and configures
 * interrupt handling for pulse counting.
 * 
 * @param config Pointer to sensor configuration structure
 * @return wind_direction_error_t Error code (WIND_DIRECTION_OK on success)
 */
wind_direction_error_t wind_direction_sensor_init(const wind_direction_config_t *config);

/**
 * @brief Deinitialize the wind direction sensor
 * 
 * Cleans up resources and disables interrupts.
 * 
 * @return wind_direction_error_t Error code (WIND_DIRECTION_OK on success)
 */
wind_direction_error_t wind_direction_sensor_deinit(void);

/**
 * @brief Get current wind direction reading
 * 
 * Calculates and returns the current wind direction based on the active hall effect sensors.
 * 
 * @param direction Pointer to store wind direction in degrees
 * @return wind_direction_error_t Error code (WIND_DIRECTION_OK on success)
 */
wind_direction_error_t wind_direction_get_reading(wind_direction_t *direction);

#endif // WIND_DIRECTION_SENSOR_H
