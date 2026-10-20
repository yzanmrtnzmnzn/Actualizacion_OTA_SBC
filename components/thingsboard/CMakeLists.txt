idf_component_register(SRCS "thingsboard_mqtt.c" "thingsboard_http.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES mqtt esp_http_client mbedtls app_events ldr_sensor json)
