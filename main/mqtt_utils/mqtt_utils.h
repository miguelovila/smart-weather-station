#ifndef MQTT_UTILS_H
#define MQTT_UTILS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_system.h"
#include "esp_log.h"
#include "mqtt_client.h"

/**
 * @brief MQTT error codes
 */
typedef enum {
    MQTT_OK = 0,
    MQTT_ERROR_INVALID_CONFIG = -1,
    MQTT_ERROR_CONNECTION_FAILED = -2,
    MQTT_ERROR_NOT_CONNECTED = -3,
    MQTT_ERROR_PUBLISH_FAILED = -4,
    MQTT_ERROR_INIT_FAILED = -5
} mqtt_error_t;

/**
 * @brief MQTT configuration structure
 */
typedef struct {
    const char* broker_url;         /**< MQTT broker URL (e.g., "mqtt://broker.example.com:1883") */
    const char* client_id;          /**< Client identifier (optional) */
    const char* username;           /**< Username for authentication (optional) */
    const char* password;           /**< Password for authentication (optional) */
} mqtt_config_t;

/**
 * @brief Connect to MQTT broker
 * 
 * Establishes a connection to the specified MQTT broker.
 * 
 * @param config Pointer to MQTT configuration structure
 * @return mqtt_error_t Error code (MQTT_OK on success)
 */
mqtt_error_t mqtt_utils_connect(const mqtt_config_t* config);

/**
 * @brief Disconnect from MQTT broker
 * 
 * Gracefully disconnects from the currently connected MQTT broker.
 * 
 * @return mqtt_error_t Error code (MQTT_OK on success)
 */
mqtt_error_t mqtt_utils_disconnect(void);

/**
 * @brief Send message to MQTT topic
 * 
 * Publishes a message to the specified topic on the connected broker.
 * 
 * @param topic The topic to publish to
 * @param message The message payload
 * @return mqtt_error_t Error code (MQTT_OK on success)
 */
mqtt_error_t mqtt_utils_publish(const char* topic, const char* message);

/**
 * @brief Check if MQTT client is connected
 * 
 * @return true if connected, false otherwise
 */
bool mqtt_utils_is_connected(void);

#endif // MQTT_UTILS_H