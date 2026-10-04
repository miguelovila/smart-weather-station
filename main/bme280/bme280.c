#include "bme280.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define TAG "BME280 Driver"

// BME280 Registers
#define BME280_REG_CHIP_ID       0xD0
#define BME280_REG_RESET         0xE0
#define BME280_REG_CTRL_HUM      0xF2
#define BME280_REG_STATUS        0xF3
#define BME280_REG_CTRL_MEAS     0xF4
#define BME280_REG_CONFIG        0xF5
#define BME280_REG_DATA_START    0xF7
#define BME280_REG_CALIB_T1_T3   0x88
#define BME280_REG_CALIB_P1_P9   0x8E
#define BME280_REG_CALIB_H1      0xA1
#define BME280_REG_CALIB_H2_H6   0xE1

// BME280 Constants
#define BME280_CHIP_ID           0x60
#define BME280_RESET_VALUE       0xB6

// Status register bits
#define BME280_STATUS_MEASURING  0x08
#define BME280_STATUS_IM_UPDATE  0x01

// Helper macros for register access
#define BME280_GET_BITS(reg_data, bitname, mask, shift) ((reg_data & mask) >> shift)
#define BME280_SET_BITS(reg_data, bitname, mask, shift, data) ((reg_data & ~mask) | ((data << shift) & mask))

// Number of data registers for complete measurement
#define BME280_DATA_LEN 8

// Function prototypes
static esp_err_t bme280_read_registers(bme280_sensor_t* sensor, uint8_t reg_addr, uint8_t* data, size_t len);
static esp_err_t bme280_write_registers(bme280_sensor_t* sensor, uint8_t reg_addr, const uint8_t* data, size_t len);
static esp_err_t bme280_read_calibration_data(bme280_sensor_t* sensor);
static void bme280_compensate_data(bme280_sensor_t* sensor, const uint8_t* raw_data, bme280_data_t* data);
static bool bme280_is_measuring(bme280_sensor_t* sensor);
static esp_err_t bme280_wait_for_measurement(bme280_sensor_t* sensor);
static esp_err_t bme280_common_init(bme280_sensor_t* sensor);

/**
 *  @brief Initialize BME280 sensor with I2C interface
 * 
 *  @param sensor Pointer to the sensor structure
 *  @param sensor_addr I2C address of the sensor
 *  @param sda_pin GPIO number for SDA
 *  @param scl_pin GPIO number for SCL
 *  @param clk_speed_hz I2C clock frequency
 * 
 *  @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_init_i2c(bme280_sensor_t* sensor, uint8_t sensor_addr, int sda_pin, int scl_pin, uint32_t clk_speed_hz) 
{
    esp_err_t ret;
    uint8_t chip_id;
    
    // Set interface type
    sensor->interface = BME280_INTERFACE_I2C;
    
    // Initialize I2C bus
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = scl_pin,
        .sda_io_num = sda_pin,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    ret = i2c_new_master_bus(&bus_config, &sensor->i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %d", ret);
        return ret;
    }
    
    // Add BME280 device to the bus
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = sensor_addr,
        .scl_speed_hz = clk_speed_hz,
    };
    
    ret = i2c_master_bus_add_device(sensor->i2c_bus_handle, &dev_config, &sensor->i2c_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device to I2C bus: %d", ret);
        i2c_del_master_bus(sensor->i2c_bus_handle);
        return ret;
    }
    
    // Check if BME280 is responding by reading chip ID
    ret = bme280_read_registers(sensor, BME280_REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %d", ret);
        bme280_free(sensor);
        return ret;
    }
    
    if (chip_id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "Invalid chip ID: 0x%02x, expected: 0x%02x", chip_id, BME280_CHIP_ID);
        bme280_free(sensor);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Complete common initialization
    return bme280_common_init(sensor);
}

/**
 *  @brief Initialize BME280 sensor with SPI interface
 * 
 *  @param sensor Pointer to the sensor structure
 *  @param host SPI host to use (SPI1_HOST, SPI2_HOST, or SPI3_HOST)
 *  @param sclk_pin GPIO number for SCLK
 *  @param miso_pin GPIO number for MISO
 *  @param mosi_pin GPIO number for MOSI
 *  @param cs_pin GPIO number for CS
 *  @param clk_speed_hz SPI clock frequency
 * 
 *  @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_init_spi(bme280_sensor_t* sensor, spi_host_device_t host, int sclk_pin, int miso_pin, int mosi_pin, int cs_pin, uint32_t clk_speed_hz) 
{
    esp_err_t ret;
    uint8_t chip_id;
    
    // Set interface type
    sensor->interface = BME280_INTERFACE_SPI;
    sensor->spi_cs_pin = cs_pin;
    
    // Initialize SPI bus configuration
    spi_bus_config_t bus_config = {
        .mosi_io_num = mosi_pin,
        .miso_io_num = miso_pin,
        .sclk_io_num = sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    
    // Initialize SPI device configuration
    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = clk_speed_hz,
        .mode = 0, // CPOL=0, CPHA=0 for SPI mode 00
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    
    // Initialize CS pin
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << cs_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CS pin: %d", ret);
        return ret;
    }
    
    // Set initial CS high (inactive)
    gpio_set_level(cs_pin, 1);
    
    // Initialize SPI bus
    ret = spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %d", ret);
        return ret;
    }
    
    // Add BME280 device to the SPI bus
    ret = spi_bus_add_device(host, &dev_config, &sensor->spi_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device to SPI bus: %d", ret);
        spi_bus_free(host);
        return ret;
    }
    
    // Check if BME280 is responding by reading chip ID
    ret = bme280_read_registers(sensor, BME280_REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID via SPI: %d", ret);
        bme280_free(sensor);
        return ret;
    }
    
    if (chip_id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "Invalid chip ID: 0x%02x, expected: 0x%02x", chip_id, BME280_CHIP_ID);
        bme280_free(sensor);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Complete common initialization
    return bme280_common_init(sensor);
}

/**
 *  @brief Common initialization code for both I2C and SPI interfaces
 * 
 *  @param sensor Pointer to the sensor structure
 *  
 *  @return esp_err_t ESP_OK on success
 */
static esp_err_t bme280_common_init(bme280_sensor_t* sensor) 
{
    esp_err_t ret;
    
    // Reset the sensor
    ret = bme280_reset(sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset sensor: %d", ret);
        bme280_free(sensor);
        return ret;
    }
    
    // Read calibration data
    ret = bme280_read_calibration_data(sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration data: %d", ret);
        bme280_free(sensor);
        return ret;
    }
    
    // Set sensor to sleep mode initially
    ret = bme280_set_mode(sensor, BME280_MODE_SLEEP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set sleep mode: %d", ret);
        bme280_free(sensor);
        return ret;
    }
    
    ESP_LOGI(TAG, "BME280 sensor initialized successfully");
    return ESP_OK;
}

/**
 *  @brief Configure BME280 sensor
 * 
 *  @param sensor Pointer to the sensor structure
 *  @param config Configuration settings
 *  
 *  @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_configure(bme280_sensor_t* sensor, const bme280_config_t* config) 
{
    esp_err_t ret;
    uint8_t reg_data;
    bme280_mode_t current_mode;
    
    // Read current mode
    ret = bme280_get_mode(sensor, &current_mode);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Sensor must be in sleep mode to configure
    if (current_mode != BME280_MODE_SLEEP) {
        ret = bme280_set_mode(sensor, BME280_MODE_SLEEP);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    // Set humidity oversampling
    reg_data = config->hum_oversampling & 0x07;
    ret = bme280_write_registers(sensor, BME280_REG_CTRL_HUM, &reg_data, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    // Set filter and standby time
    ret = bme280_read_registers(sensor, BME280_REG_CONFIG, &reg_data, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Set filter (bits 4:2)
    reg_data = BME280_SET_BITS(reg_data, FILTER, 0x1C, 2, config->filter);
    // Set standby time (bits 7:5)
    reg_data = BME280_SET_BITS(reg_data, STANDBY, 0xE0, 5, config->standby_time);
    
    ret = bme280_write_registers(sensor, BME280_REG_CONFIG, &reg_data, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Set temperature and pressure oversampling and mode
    ret = bme280_read_registers(sensor, BME280_REG_CTRL_MEAS, &reg_data, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Set temperature oversampling (bits 7:5)
    reg_data = BME280_SET_BITS(reg_data, TEMP, 0xE0, 5, config->temp_oversampling);
    // Set pressure oversampling (bits 4:2)
    reg_data = BME280_SET_BITS(reg_data, PRESS, 0x1C, 2, config->press_oversampling);
    // Set to sleep mode (bits 1:0)
    reg_data = BME280_SET_BITS(reg_data, MODE, 0x03, 0, BME280_MODE_SLEEP);
    
    ret = bme280_write_registers(sensor, BME280_REG_CTRL_MEAS, &reg_data, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ESP_LOGI(TAG, "BME280 configured with: T=%d, P=%d, H=%d, filter=%d, standby=%d", config->temp_oversampling, config->press_oversampling,  config->hum_oversampling, config->filter, config->standby_time);
    
    return ESP_OK;
}

/**
 *  @brief Set sensor mode (sleep, forced, normal)
 *
 *  @param sensor Pointer to the sensor structure
 *  @param mode Operation mode
 *
 *  @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_set_mode(bme280_sensor_t* sensor, bme280_mode_t mode) 
{
    esp_err_t ret;
    uint8_t reg_data;
    
    // Read current control register
    ret = bme280_read_registers(sensor, BME280_REG_CTRL_MEAS, &reg_data, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Update mode bits (1:0) while preserving other settings
    reg_data = BME280_SET_BITS(reg_data, MODE, 0x03, 0, mode);
    
    // Write updated control register
    return bme280_write_registers(sensor, BME280_REG_CTRL_MEAS, &reg_data, 1);
}

/**
 *  @brief Get current sensor mode
 * 
 *  @param sensor Pointer to the sensor structure
 *  @param mode Pointer to store the current mode
 * 
 *  @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_get_mode(bme280_sensor_t* sensor, bme280_mode_t* mode) 
{
    esp_err_t ret;
    uint8_t reg_data;
    
    ret = bme280_read_registers(sensor, BME280_REG_CTRL_MEAS, &reg_data, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    
    *mode = (bme280_mode_t)(reg_data & 0x03);
    return ESP_OK;
}

/**
 *  @brief Read sensor data (temperature, pressure, humidity)
 * 
 *  @param sensor Pointer to the sensor structure
 *  @param data Pointer to store the measurement data
 * 
 *  @return esp_err_t ESP_OK on success
 *  @return esp_err_t ESP_ERR_TIMEOUT if measurement takes too long
 */
esp_err_t bme280_read_data(bme280_sensor_t* sensor, bme280_data_t* data) 
{
    esp_err_t ret;
    uint8_t raw_data[BME280_DATA_LEN];
    
    // Wait until measurement is complete (if measurement is in progress)
    ret = bme280_wait_for_measurement(sensor);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Read raw measurement data (pressure, temperature, humidity)
    ret = bme280_read_registers(sensor, BME280_REG_DATA_START, raw_data, BME280_DATA_LEN);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Compensate raw values using calibration data
    bme280_compensate_data(sensor, raw_data, data);
    
    return ESP_OK;
}

/**
 *  @brief Trigger a forced measurement and read data
 *  
 *  @param sensor Pointer to the sensor structure
 *  @param data Pointer to store the measurement data
 *  
 *  @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_forced_read(bme280_sensor_t* sensor, bme280_data_t* data) 
{
    esp_err_t ret;
    
    // Set to forced mode to trigger a measurement
    ret = bme280_set_mode(sensor, BME280_MODE_FORCED);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Wait for the measurement to complete
    ret = bme280_wait_for_measurement(sensor);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Read the measurement data
    return bme280_read_data(sensor, data);
}

/**
 *  @brief Reset the BME280 sensor
 *  
 *  @param sensor Pointer to the sensor structure
 *  
 *  @return esp_err_t ESP_OK on success
 *  @return esp_err_t ESP_ERR_TIMEOUT if reset takes too long
 *  @return esp_err_t ESP_ERR_INVALID_ARG if invalid argument is passed
 *  @return esp_err_t ESP_ERR_INVALID_RESPONSE if invalid response is received
 */
esp_err_t bme280_reset(bme280_sensor_t* sensor) 
{
    esp_err_t ret;
    uint8_t reset_cmd = BME280_RESET_VALUE;
    uint8_t status;
    int retry = 35;
    
    // Send reset command
    ret = bme280_write_registers(sensor, BME280_REG_RESET, &reset_cmd, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    // Wait for reset to complete (NVM copy bit to clear)
    do {
        vTaskDelay(pdMS_TO_TICKS(5));
        
        ret = bme280_read_registers(sensor, BME280_REG_STATUS, &status, 1);
        if (ret != ESP_OK) {
            return ret;
        }
        
        if (!(status & BME280_STATUS_IM_UPDATE)) {
            return ESP_OK; // Reset complete
        }
        
        retry--;
    } while (retry > 0);
    
    return ESP_ERR_TIMEOUT;
}

/**
 *  @brief Free BME280 sensor resources
 * 
 *  @param sensor Pointer to the sensor structure
 * 
 *  @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_free(bme280_sensor_t* sensor) 
{
    esp_err_t ret = ESP_OK;
    
    if (sensor->interface == BME280_INTERFACE_I2C) {
        // Free I2C resources
        ret = i2c_master_bus_rm_device(sensor->i2c_dev_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove I2C device: %d", ret);
            return ret;
        }
        
        ret = i2c_del_master_bus(sensor->i2c_bus_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete I2C bus: %d", ret);
            return ret;
        }
    } else if (sensor->interface == BME280_INTERFACE_SPI) {
        // Free SPI resources
        spi_bus_remove_device(sensor->spi_dev_handle);
    }
    
    return ESP_OK;
}

/**
 *  @brief Read registers from BME280
 * 
 *  @param sensor Pointer to the sensor structure
 *  @param reg_addr Register address to read from
 *  @param data Pointer to store the read data
 *  @param len Number of bytes to read
 * 
 *  @return esp_err_t ESP_OK on success
 */
static esp_err_t bme280_read_registers(bme280_sensor_t* sensor, uint8_t reg_addr, uint8_t* data, size_t len) 
{
    if (sensor->interface == BME280_INTERFACE_I2C) {
        // For I2C: transmit register address, then receive data
        return i2c_master_transmit_receive(sensor->i2c_dev_handle, &reg_addr, 1, data, len, -1);
    } else {
        // For SPI: set read bit, activate CS, transmit address, receive data, deactivate CS
        esp_err_t ret;
        spi_transaction_t t = {0};
        uint8_t tx_data[len + 1];
        uint8_t rx_data[len + 1];
        
        // Set read bit in register address (bit 7)
        tx_data[0] = reg_addr | BME280_SPI_READ_BIT;
        memset(tx_data + 1, 0, len);
        
        t.length = 8 * (len + 1);
        t.tx_buffer = tx_data;
        t.rx_buffer = rx_data;
        
        // Perform SPI transaction
        gpio_set_level(sensor->spi_cs_pin, 0);
        ret = spi_device_transmit(sensor->spi_dev_handle, &t);
        gpio_set_level(sensor->spi_cs_pin, 1);
        
        if (ret == ESP_OK) {
            // Copy received data igonoring first byte which is just the echo of the address
            memcpy(data, rx_data + 1, len);
        }
        
        return ret;
    }
}

/**
 *  @brief Write registers to BME280
 * 
 *  @param sensor Pointer to the sensor structure
 *  @param reg_addr Register address to write to
 *  @param data Pointer to the data to write
 *  @param len Number of bytes to write
 * 
 *  @return esp_err_t ESP_OK on success
 *  @return esp_err_t ESP_ERR_NO_MEM if memory allocation fails
 *  @return esp_err_t ESP_ERR_TIMEOUT if transaction times out
 */
static esp_err_t bme280_write_registers(bme280_sensor_t* sensor, uint8_t reg_addr, const uint8_t* data, size_t len) 
{
    if (sensor->interface == BME280_INTERFACE_I2C) {
        // For I2C: prepare buffer with register address + data
        esp_err_t ret;
        uint8_t *buffer;
        
        buffer = malloc(len + 1);
        if (buffer == NULL) {
            return ESP_ERR_NO_MEM;
        }
        
        buffer[0] = reg_addr;
        memcpy(buffer + 1, data, len);
        
        ret = i2c_master_transmit(sensor->i2c_dev_handle, buffer, len + 1, -1);
        
        free(buffer);
        return ret;
    } else {
        // For SPI: write bit is clear (bit 7), activate CS, transmit address + data, deactivate CS
        esp_err_t ret;
        spi_transaction_t t = {0};
        uint8_t tx_data[len + 1];
        
        // Clear bit 7 to indicate write operation
        tx_data[0] = reg_addr & (~BME280_SPI_READ_BIT);
        memcpy(tx_data + 1, data, len);
        
        t.length = 8 * (len + 1);
        t.tx_buffer = tx_data;
        
        // Perform SPI transaction
        gpio_set_level(sensor->spi_cs_pin, 0);
        ret = spi_device_transmit(sensor->spi_dev_handle, &t);
        gpio_set_level(sensor->spi_cs_pin, 1);
        
        return ret;
    }
}

/**
 *  @brief Check if BME280 is currently measuring
 * 
 *  @param sensor Pointer to the sensor structure
 * 
 *  @return bool True if measuring, false otherwise
 */
static bool bme280_is_measuring(bme280_sensor_t* sensor) 
{
    uint8_t status;
    
    if (bme280_read_registers(sensor, BME280_REG_STATUS, &status, 1) != ESP_OK) {
        return false;
    }
    
    return (status & BME280_STATUS_MEASURING) != 0;
}

/**
 *  @brief Wait for measurement to complete
 * 
 *  @param sensor Pointer to the sensor structure
 * 
 *  @return esp_err_t ESP_OK on success
 *  @return esp_err_t ESP_ERR_TIMEOUT if measurement takes too long
 */
static esp_err_t bme280_wait_for_measurement(bme280_sensor_t* sensor) 
{
    int retry = 35; // Timeout after 35 checks
    
    while (bme280_is_measuring(sensor) && retry > 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
        retry--;
    }
    
    if (retry == 0) {
        ESP_LOGE(TAG, "Timeout waiting for measurement to complete");
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

/**
 *  @brief Read calibration data from BME280 as specified in the datasheet
 * 
 *  @param sensor Pointer to the sensor structure
 * 
 *  @return esp_err_t ESP_OK on success
 */
static esp_err_t bme280_read_calibration_data(bme280_sensor_t* sensor) 
{
    esp_err_t ret;
    uint8_t buffer[26];
    
    // Read temperature and pressure calibration data
    ret = bme280_read_registers(sensor, BME280_REG_CALIB_T1_T3, buffer, 6);
    if (ret != ESP_OK) {
        return ret;
    }
    
    sensor->calib_data.dig_T1 = (uint16_t)((buffer[1] << 8) | buffer[0]);
    sensor->calib_data.dig_T2 = ( int16_t)((buffer[3] << 8) | buffer[2]);
    sensor->calib_data.dig_T3 = ( int16_t)((buffer[5] << 8) | buffer[4]);
    
    // Read pressure calibration data
    ret = bme280_read_registers(sensor, BME280_REG_CALIB_P1_P9, buffer, 18);
    if (ret != ESP_OK) {
        return ret;
    }
    
    sensor->calib_data.dig_P1 = (uint16_t)((buffer[1]  << 8) | buffer[0]);
    sensor->calib_data.dig_P2 = ( int16_t)((buffer[3]  << 8) | buffer[2]);
    sensor->calib_data.dig_P3 = ( int16_t)((buffer[5]  << 8) | buffer[4]);
    sensor->calib_data.dig_P4 = ( int16_t)((buffer[7]  << 8) | buffer[6]);
    sensor->calib_data.dig_P5 = ( int16_t)((buffer[9]  << 8) | buffer[8]);
    sensor->calib_data.dig_P6 = ( int16_t)((buffer[11] << 8) | buffer[10]);
    sensor->calib_data.dig_P7 = ( int16_t)((buffer[13] << 8) | buffer[12]);
    sensor->calib_data.dig_P8 = ( int16_t)((buffer[15] << 8) | buffer[14]);
    sensor->calib_data.dig_P9 = ( int16_t)((buffer[17] << 8) | buffer[16]);
    
    // Read humidity calibration data
    ret = bme280_read_registers(sensor, BME280_REG_CALIB_H1, &sensor->calib_data.dig_H1, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = bme280_read_registers(sensor, BME280_REG_CALIB_H2_H6, buffer, 7);
    if (ret != ESP_OK) {
        return ret;
    }
    
    sensor->calib_data.dig_H2 = (int16_t)((buffer[1] << 8) | buffer[0]);
    sensor->calib_data.dig_H3 = buffer[2];
    
    sensor->calib_data.dig_H4 = (int16_t)((buffer[3] << 4) | (buffer[4] & 0x0F));
    sensor->calib_data.dig_H5 = (int16_t)((buffer[5] << 4) | (buffer[4] >> 4));
    
    sensor->calib_data.dig_H6 = (int8_t)buffer[6];
    
    ESP_LOGI(TAG, "BME280 calibration data read successfully");
    return ESP_OK;
}

/**
 *  @brief Compensate raw data using calibration data like
 *  specified in the BME280 datasheet page 50.
 * 
 *  @param sensor Pointer to the sensor structure
 *  @param raw_data Pointer to the raw data read from the sensor
 *  @param data Pointer to store the compensated data
 * 
 *  @return void
 */
static void bme280_compensate_data(bme280_sensor_t* sensor, const uint8_t* raw_data, bme280_data_t* data) 
{
    int32_t temp_raw, press_raw, hum_raw;
    int32_t t_fine;
    
    // Parse raw data
    press_raw = ((uint32_t)raw_data[0] << 12) | ((uint32_t)raw_data[1] << 4) | ((uint32_t)raw_data[2] >> 4);
    temp_raw = ((uint32_t)raw_data[3] << 12) | ((uint32_t)raw_data[4] << 4) | ((uint32_t)raw_data[5] >> 4);
    hum_raw = ((uint32_t)raw_data[6] << 8) | (uint32_t)raw_data[7];
    
    // Compensate temperature
    {
        int32_t var1, var2;
        
        var1 = ((((temp_raw >> 3) - ((int32_t)sensor->calib_data.dig_T1 << 1))) * 
                ((int32_t)sensor->calib_data.dig_T2)) >> 11;
                
        var2 = (((((temp_raw >> 4) - ((int32_t)sensor->calib_data.dig_T1)) * 
                ((temp_raw >> 4) - ((int32_t)sensor->calib_data.dig_T1))) >> 12) * 
                ((int32_t)sensor->calib_data.dig_T3)) >> 14;
                
        t_fine = var1 + var2;
        sensor->calib_data.t_fine = t_fine; // Save for pressure and humidity calculations
        
        data->temperature = (float)((t_fine * 5 + 128) >> 8) / 100.0f;
    }
    
    // Compensate pressure
    if (press_raw != 0x80000) { // Check if pressure measurement was enabled
        int64_t var1, var2, p;
        
        var1 = ((int64_t)t_fine) - 128000;
        var2 = var1 * var1 * (int64_t)sensor->calib_data.dig_P6;
        var2 = var2 + ((var1 * (int64_t)sensor->calib_data.dig_P5) << 17);
        var2 = var2 + (((int64_t)sensor->calib_data.dig_P4) << 35);
        var1 = ((var1 * var1 * (int64_t)sensor->calib_data.dig_P3) >> 8) + 
               ((var1 * (int64_t)sensor->calib_data.dig_P2) << 12);
        var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)sensor->calib_data.dig_P1) >> 33;
        
        if (var1 == 0) {
            data->pressure = 0.0f; // Avoid division by zero
        } else {
            p = 1048576 - press_raw;
            p = (((p << 31) - var2) * 3125) / var1;
            var1 = (((int64_t)sensor->calib_data.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
            var2 = (((int64_t)sensor->calib_data.dig_P8) * p) >> 19;
            
            p = ((p + var1 + var2) >> 8) + (((int64_t)sensor->calib_data.dig_P7) << 4);
            data->pressure = (float)p / 256000.0f; // Convert to hPa
        }
    } else {
        data->pressure = 0.0f;
    }
    
    // Compensate humidity
    if (hum_raw != 0x8000) { // Check if humidity measurement was enabled
        int32_t v_x1_u32r;
        
        v_x1_u32r = t_fine - ((int32_t)76800);
        
        v_x1_u32r = (((((hum_raw << 14) - (((int32_t)sensor->calib_data.dig_H4) << 20) - 
                    (((int32_t)sensor->calib_data.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) * 
                    (((((((v_x1_u32r * ((int32_t)sensor->calib_data.dig_H6)) >> 10) * 
                    (((v_x1_u32r * ((int32_t)sensor->calib_data.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + 
                    ((int32_t)2097152)) * ((int32_t)sensor->calib_data.dig_H2) + 8192) >> 14));
        
        v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * 
                    ((int32_t)sensor->calib_data.dig_H1)) >> 4));
        
        v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
        v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;
        
        data->humidity = (float)(v_x1_u32r >> 12) / 1024.0f;
        
        // Ensure valid range 0-100%
        if (data->humidity > 100.0f) {
            data->humidity = 100.0f;
        } else if (data->humidity < 0.0f) {
            data->humidity = 0.0f;
        }
    } else {
        data->humidity = 0.0f;
    }
}