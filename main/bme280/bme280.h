#ifndef __BME280_SENSOR_H__INCLUDED__
#define __BME280_SENSOR_H__INCLUDED__

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include <stdbool.h>

// BME280 I2C Addresses
#define BME280_I2C_ADDR_PRIMARY    0x76
#define BME280_I2C_ADDR_SECONDARY  0x77
#define BME280_SCL_DEFAULT_FREQ_HZ 100000

// BME280 SPI Configuration
#define BME280_SPI_DEFAULT_FREQ_HZ 1000000  // 1 MHz
#define BME280_SPI_READ_BIT        0x80     // Read bit for SPI register address

// BME280 Interface Types
typedef enum {
    BME280_INTERFACE_I2C = 0,
    BME280_INTERFACE_SPI = 1
} bme280_interface_t;

// BME280 measurement modes
typedef enum {
    BME280_MODE_SLEEP  = 0x00,
    BME280_MODE_FORCED = 0x01,
    BME280_MODE_NORMAL = 0x03
} bme280_mode_t;

// BME280 oversampling settings
typedef enum {
    BME280_OVERSAMPLING_NONE = 0x00,
    BME280_OVERSAMPLING_1X   = 0x01,
    BME280_OVERSAMPLING_2X   = 0x02,
    BME280_OVERSAMPLING_4X   = 0x03,
    BME280_OVERSAMPLING_8X   = 0x04,
    BME280_OVERSAMPLING_16X  = 0x05
} bme280_oversampling_t;

// BME280 filter settings
typedef enum {
    BME280_FILTER_OFF = 0x00,
    BME280_FILTER_2   = 0x01,
    BME280_FILTER_4   = 0x02,
    BME280_FILTER_8   = 0x03,
    BME280_FILTER_16  = 0x04
} bme280_filter_t;

// BME280 standby time settings for normal mode
typedef enum {
    BME280_STANDBY_0_5_MS  = 0x00,
    BME280_STANDBY_62_5_MS = 0x01,
    BME280_STANDBY_125_MS  = 0x02,
    BME280_STANDBY_250_MS  = 0x03,
    BME280_STANDBY_500_MS  = 0x04,
    BME280_STANDBY_1000_MS = 0x05,
    BME280_STANDBY_10_MS   = 0x06,
    BME280_STANDBY_20_MS   = 0x07
} bme280_standby_t;

// BME280 sensor configuration
typedef struct {
    bme280_oversampling_t temp_oversampling;
    bme280_oversampling_t press_oversampling;
    bme280_oversampling_t hum_oversampling;
    bme280_filter_t filter;
    bme280_standby_t standby_time;
} bme280_config_t;

// BME280 measurement data
typedef struct {
    float temperature; // in °C
    float pressure;    // in hPa (hectopascals)
    float humidity;    // in % relative humidity
} bme280_data_t;

// Calibration data structure
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;
    int32_t  t_fine; // Temperature fine value used in calculations
} bme280_calib_data_t;

// BME280 device handle
typedef struct {
    bme280_interface_t interface;     // Interface type (I2C or SPI)
    
    // I2C interface handles
    i2c_master_bus_handle_t i2c_bus_handle;
    i2c_master_dev_handle_t i2c_dev_handle;
    
    // SPI interface handles
    spi_device_handle_t spi_dev_handle;
    int spi_cs_pin;                   // GPIO pin for SPI CS
    
    // Calibration data
    bme280_calib_data_t calib_data;
} bme280_sensor_t;

/**
 * @brief Initialize BME280 sensor with I2C interface
 * 
 * @param sensor Pointer to the sensor structure
 * @param sensor_addr I2C address of the sensor (BME280_I2C_ADDR_PRIMARY or BME280_I2C_ADDR_SECONDARY)
 * @param sda_pin GPIO number for SDA
 * @param scl_pin GPIO number for SCL
 * @param clk_speed_hz I2C clock frequency
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_init_i2c(bme280_sensor_t* sensor, uint8_t sensor_addr, 
                          int sda_pin, int scl_pin, uint32_t clk_speed_hz);

/**
 * @brief Initialize BME280 sensor with SPI interface
 * 
 * @param sensor Pointer to the sensor structure
 * @param host SPI host to use (SPI1_HOST, SPI2_HOST, or SPI3_HOST)
 * @param sclk_pin GPIO number for SCLK
 * @param miso_pin GPIO number for MISO
 * @param mosi_pin GPIO number for MOSI
 * @param cs_pin GPIO number for CS
 * @param clk_speed_hz SPI clock frequency
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_init_spi(bme280_sensor_t* sensor, spi_host_device_t host, 
                          int sclk_pin, int miso_pin, int mosi_pin, int cs_pin, 
                          uint32_t clk_speed_hz);

/**
 * @brief Configure BME280 sensor
 * 
 * @param sensor Pointer to the sensor structure
 * @param config Configuration settings
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_configure(bme280_sensor_t* sensor, const bme280_config_t* config);

/**
 * @brief Set sensor mode (sleep, forced, normal)
 * 
 * @param sensor Pointer to the sensor structure
 * @param mode Operation mode
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_set_mode(bme280_sensor_t* sensor, bme280_mode_t mode);

/**
 * @brief Get current sensor mode
 * 
 * @param sensor Pointer to the sensor structure
 * @param mode Pointer to store the current mode
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_get_mode(bme280_sensor_t* sensor, bme280_mode_t* mode);

/**
 * @brief Read sensor data (temperature, pressure, humidity)
 * 
 * @param sensor Pointer to the sensor structure
 * @param data Pointer to store the measurement data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_read_data(bme280_sensor_t* sensor, bme280_data_t* data);

/**
 * @brief Trigger a forced measurement and read data
 * 
 * @param sensor Pointer to the sensor structure
 * @param data Pointer to store the measurement data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_forced_read(bme280_sensor_t* sensor, bme280_data_t* data);

/**
 * @brief Reset the BME280 sensor
 * 
 * @param sensor Pointer to the sensor structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_reset(bme280_sensor_t* sensor);

/**
 * @brief Free resources associated with the BME280 sensor
 * 
 * @param sensor Pointer to the sensor structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bme280_free(bme280_sensor_t* sensor);

#endif // __BME280_SENSOR_H__INCLUDED__