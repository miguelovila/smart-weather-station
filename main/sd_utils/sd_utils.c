#include "sd_utils.h"
#include <dirent.h>
#include <errno.h>
#include "ff.h"

static const char *TAG = "sd_utils";

static bool internal_sd_initialized = false;
static char internal_mount_point[32] = "/sdcard";
static sdmmc_card_t* internal_card = NULL;

sd_error_t sd_init(const sd_config_t* config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return SD_ERROR_INVALID_PARAM;
    }

    if (internal_sd_initialized) {
        ESP_LOGW(TAG, "SD card already initialized");
        return SD_OK;
    }

    strncpy(internal_mount_point, config->mount_point, sizeof(internal_mount_point));

    ESP_LOGI(TAG, "Initializing SD card");

    // Options for mounting the filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = config->format_if_mount_failed,
        .max_files = config->max_files,
        .allocation_unit_size = 16 * 1024
    };

    // Use settings defined above to initialize SD card and mount FAT filesystem
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.command_timeout_ms = 1000; // Set command timeout to 1 second
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = config->miso_pin,
        .sclk_io_num = config->clk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus: %s", esp_err_to_name(ret));
        return SD_ERROR_INIT;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = config->cs_pin;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount(internal_mount_point, &host, &slot_config, &mount_config, &internal_card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        spi_bus_free(host.slot);
        return SD_ERROR_MOUNT;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, internal_card);

    internal_sd_initialized = true;
    ESP_LOGI(TAG, "SD card mounted at %s", internal_mount_point);

    return SD_OK;
}

sd_error_t sd_deinit(void)
{
    if (!internal_sd_initialized) {
        ESP_LOGW(TAG, "SD card not initialized");
        return SD_OK;
    }

    ESP_LOGI(TAG, "Unmounting SD card");
    
    // All done, unmount partition and disable SDMMC or SPI peripheral
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(internal_mount_point, internal_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
        return SD_ERROR_UNMOUNT;
    }
    
    // Free SPI bus
    spi_bus_free(SDSPI_DEFAULT_HOST);

    internal_sd_initialized = false;
    internal_card = NULL;
    
    ESP_LOGI(TAG, "SD card unmounted");
    return SD_OK;
}

sd_error_t sd_append_text_file(const char* filename, const char* content)
{
    if (!internal_sd_initialized) {
        ESP_LOGE(TAG, "SD card not initialized");
        return SD_ERROR_INIT;
    }

    if (filename == NULL || content == NULL) {
        return SD_ERROR_INVALID_PARAM;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", internal_mount_point, filename);

    FILE* f = fopen(full_path, "a");
    if (f == NULL) {
        if (errno == ENOENT) {
            f = fopen(full_path, "w");
            if (f == NULL) {
                ESP_LOGE(TAG, "Failed to create file: %s", full_path);
                return SD_ERROR_FILE_OPEN;
            }
        } else {
            ESP_LOGE(TAG, "Failed to open file for appending: %s", full_path);
            ESP_LOGE(TAG, "Error: %s", strerror(errno));
            return SD_ERROR_FILE_OPEN;
        }
    }

    size_t written = fwrite(content, 1, strlen(content), f);
    fclose(f);

    if (written != strlen(content)) {
        ESP_LOGE(TAG, "Failed to append complete content to file");
        return SD_ERROR_FILE_WRITE;
    }

    ESP_LOGI(TAG, "Content appended to file: %s (%zu bytes)", full_path, written);
    return SD_OK;
}

sd_error_t sd_read_text_file(const char* filename, char* buffer, size_t buffer_size, size_t* bytes_read)
{
    if (!internal_sd_initialized) {
        ESP_LOGE(TAG, "SD card not initialized");
        return SD_ERROR_INIT;
    }

    if (filename == NULL || buffer == NULL || buffer_size == 0) {
        return SD_ERROR_INVALID_PARAM;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", internal_mount_point, filename);

    FILE* f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", full_path);
        return SD_ERROR_FILE_NOT_FOUND;
    }

    size_t read_bytes = fread(buffer, 1, buffer_size - 1, f);
    buffer[read_bytes] = '\0';
    fclose(f);

    if (bytes_read) {
        *bytes_read = read_bytes;
    }

    ESP_LOGI(TAG, "File read: %s (%zu bytes)", full_path, read_bytes);
    return SD_OK;
}

bool sd_file_exists(const char* filename)
{
    if (!internal_sd_initialized || filename == NULL) {
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", internal_mount_point, filename);

    struct stat st;
    return stat(full_path, &st) == 0;
}

sd_error_t sd_get_wifi_settings(char* ssid_buf, size_t ssid_buf_size, char* password_buf, size_t password_buf_size)
{
    if (!internal_sd_initialized) {
        ESP_LOGE(TAG, "SD card not initialized");
        return SD_ERROR_INIT;
    }

    if (ssid_buf == NULL || password_buf == NULL || ssid_buf_size == 0 || password_buf_size == 0) {
        return SD_ERROR_INVALID_PARAM;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/wifi_settings.txt", internal_mount_point);

    FILE* f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open WiFi settings file: %s", full_path);
        return SD_ERROR_FILE_NOT_FOUND;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ssid=", 5) == 0) {
            strncpy(ssid_buf, line + 5, ssid_buf_size - 1);
            ssid_buf[ssid_buf_size - 1] = '\0';
            char *newline = strchr(ssid_buf, '\n');
            if (newline) *newline = '\0';
        } else if (strncmp(line, "password=", 9) == 0) {
            strncpy(password_buf, line + 9, password_buf_size - 1);
            password_buf[password_buf_size - 1] = '\0';
            char *newline = strchr(password_buf, '\n');
            if (newline) *newline = '\0';
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "WiFi settings read successfully");
    return SD_OK;
}

sd_error_t sd_get_mqtt_settings(char* broker_url_buf, size_t broker_url_buf_size, char* client_id_buf, size_t client_id_buf_size)
{
    if (!internal_sd_initialized) {
        ESP_LOGE(TAG, "SD card not initialized");
        return SD_ERROR_INIT;
    }

    if (broker_url_buf == NULL || client_id_buf == NULL || broker_url_buf_size == 0 || client_id_buf_size == 0) {
        return SD_ERROR_INVALID_PARAM;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/mqtt_settings.txt", internal_mount_point);

    FILE* f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open MQTT settings file: %s", full_path);
        return SD_ERROR_FILE_NOT_FOUND;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "broker_url=", 11) == 0) {
            strncpy(broker_url_buf, line + 11, broker_url_buf_size - 1);
            broker_url_buf[broker_url_buf_size - 1] = '\0';
            char *newline = strchr(broker_url_buf, '\n');
            if (newline) *newline = '\0';
        } else if (strncmp(line, "client_id=", 10) == 0) {
            strncpy(client_id_buf, line + 10, client_id_buf_size - 1);
            client_id_buf[client_id_buf_size - 1] = '\0';
            char *newline = strchr(client_id_buf, '\n');
            if (newline) *newline = '\0';
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "MQTT settings read successfully");
    return SD_OK;
}
