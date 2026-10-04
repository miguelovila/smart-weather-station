#ifndef DATA_HANDLER_H
#define DATA_HANDLER_H

#include "esp_log.h"
#include "wind_direction/wind_direction_sensor.h"
#include "mqtt_utils/mqtt_utils.h"
#include "sd_utils/sd_utils.h"
#include "bme280/bme280.h"

void handle_bme280_data(bme280_data_t *data);
void handle_wva420_data(wind_direction_t *data);
void handle_wsp420_data(float speed);

#endif // DATA_HANDLER_H