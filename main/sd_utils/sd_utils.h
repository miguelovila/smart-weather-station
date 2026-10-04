#ifndef SD_UTILS_H
#define SD_UTILS_H

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

// SD card configuration structure
typedef struct {
    int miso_pin;               // MISO pin
    int mosi_pin;               // MOSI pin
    int clk_pin;                // CLK pin
    int cs_pin;                 // CS pin
    const char* mount_point;    // Mount point (e.g., "/sdcard")
    int max_files;              // Maximum number of open files
    bool format_if_mount_failed; // Format if mount fails
} sd_config_t;

// Error codes
typedef enum {
    SD_OK = 0,
    SD_ERROR_INIT = -1,
    SD_ERROR_MOUNT = -2,
    SD_ERROR_FILE_OPEN = -3,
    SD_ERROR_FILE_WRITE = -4,
    SD_ERROR_FILE_NOT_FOUND = -5,
    SD_ERROR_INVALID_PARAM = -6,
    SD_ERROR_UNMOUNT = -7
} sd_error_t;

typedef struct sd_utils
{
    /* configuration */
    sd_config_t config;
    
    /* internal state */
    bool initialized;
    char mount_point[32];
    sdmmc_card_t* card;
} sd_utils_t;

/**
 * @brief Initialize SD card with SPI interface
 * 
 * @param config SD card configuration
 * @return sd_error_t SD_OK on success, error code otherwise
 */
sd_error_t sd_init(const sd_config_t* config);

/**
 * @brief Deinitialize SD card and unmount filesystem
 * 
 * @return sd_error_t SD_OK on success, error code otherwise
 */
sd_error_t sd_deinit(void);

/**
 * @brief Append text to a file (create if it doesn't exist)
 * 
 * @param filename Full path to the file
 * @param content Text content to append
 * @return sd_error_t SD_OK on success, error code otherwise
 */
sd_error_t sd_append_text_file(const char* filename, const char* content);

/**
 * @brief Read entire text file into a buffer
 * 
 * @param filename Full path to the file
 * @param buffer Buffer to store the content (must be pre-allocated)
 * @param buffer_size Size of the buffer
 * @param bytes_read Number of bytes actually read (output, can be NULL)
 * @return sd_error_t SD_OK on success, error code otherwise
 */
sd_error_t sd_read_text_file(const char* filename, char* buffer, size_t buffer_size, size_t* bytes_read);

/**
 * @brief Check if a file exists
 * 
 * @param filename Full path to the file
 * @return true if file exists, false otherwise
 */
bool sd_file_exists(const char* filename);

/**
 * @brief Get WiFi settings from a file
 * 
 * @param ssid_buf Buffer to store the SSID (must be pre-allocated)
 * @param ssid_buf_size Size of the SSID buffer
 * @param password_buf Buffer to store the password (must be pre-allocated)
 * @param password_buf_size Size of the password buffer
 * @return sd_error_t SD_OK on success, error code otherwise
 */
sd_error_t sd_get_wifi_settings(char* ssid_buf, size_t ssid_buf_size, char* password_buf, size_t password_buf_size);

/**
 * @brief Get MQTT settings from a file
 * 
 * @param broker_url_buf Buffer to store the broker URL (must be pre-allocated)
 * @param broker_url_buf_size Size of the broker URL buffer
 * @param client_id_buf Buffer to store the client ID (must be pre-allocated)
 * @param client_id_buf_size Size of the client ID buffer
 * @return sd_error_t SD_OK on success, error code otherwise
 */
sd_error_t sd_get_mqtt_settings(char* broker_url_buf, size_t broker_url_buf_size, char* client_id_buf, size_t client_id_buf_size);

#endif // SD_UTILS_H