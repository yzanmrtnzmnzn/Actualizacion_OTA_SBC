idf_component_register(SRCS "ldr_sensor.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_events
                       PRIV_REQUIRES esp_adc)
