#ifndef WIFI_UTILS_H
#define WIFI_UTILS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

/**
 * @brief WiFi connection configuration structure
 */
typedef struct {
    char ssid[32];                    // WiFi network name
    char password[64];                // WiFi password
    uint32_t retry_interval_ms;       // Retry interval in milliseconds (default: 5000)
    uint8_t max_retry_attempts;       // Maximum retry attempts (0 = infinite)
    // bool auto_reconnect;              // Enable automatic reconnection
} wifi_utils_config_t;

/**
 * @brief WiFi connection status
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_FAILED
} wifi_status_t;

/**
 * @brief WiFi error codes
 */
typedef enum {
    WIFI_OK = 0,
    WIFI_ERROR_INVALID_CONFIG = -1,
    WIFI_ERROR_INIT_FAILED = -2,
    WIFI_ERROR_CONNECTION_FAILED = -3,
    WIFI_ERROR_NOT_INITIALIZED = -4
} wifi_error_t;

/**
 * @brief Initialize WiFi utilities
 * 
 * Initializes the WiFi stack, event loop, and netif.
 * Must be called before any other WiFi functions.
 * 
 * @return wifi_error_t Error code (WIFI_OK on success)
 */
wifi_error_t wifi_utils_init(void);

/**
 * @brief Connect to WiFi network
 * 
 * Connects to the specified WiFi network with retry logic.
 * 
 * @param config WiFi configuration structure
 * @return wifi_error_t Error code (WIFI_OK on success)
 */
wifi_error_t wifi_utils_connect(const wifi_utils_config_t *config);

/**
 * @brief Disconnect from WiFi network
 * 
 * Disconnects from the current WiFi network.
 * 
 * @return wifi_error_t Error code (WIFI_OK on success)
 */
wifi_error_t wifi_utils_disconnect(void);

/**
 * @brief Get current WiFi connection status
 * 
 * @return wifi_status_t Current connection status
 */
wifi_status_t wifi_utils_get_status(void);

/**
 * @brief Check if WiFi is connected
 * 
 * @return true if connected, false otherwise
 */
bool wifi_utils_is_connected(void);

/**
 * @brief Get WiFi IP Address
 * 
 * @param ip_addr Buffer to store IP address string (minimum 16 bytes)
 * @return wifi_error_t Error code (WIFI_OK on success)
 */
wifi_error_t wifi_utils_get_ip_addr(char *ip_addr);

/**
 * @brief Deinitialize WiFi utilities
 * 
 * Cleans up WiFi resources and disconnects if connected.
 * 
 * @return wifi_error_t Error code (WIFI_OK on success)
 */
wifi_error_t wifi_utils_deinit(void);

#endif // WIFI_UTILS_H