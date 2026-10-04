idf_component_register(SRCS "main.c"
                            "bme280/bme280.c"
                            "sd_utils/sd_utils.c"
                            "wifi_utils/wifi_utils.c"
                            "mqtt_utils/mqtt_utils.c"
                            "data_handler/data_handler.c"
                            "wind_direction/wind_direction_sensor.c"
                            "wind_speed/wind_speed_sensor.c"

                    INCLUDE_DIRS "." "wifi_utils" "mqtt_utils" "sd_utils" "bme280" "wind_direction" "wind_speed" "data_handler")