idf_component_register(
    SRCS "ota_update.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_https_ota
        esp_http_client
        esp_partition
        mbedtls
        esp_system
        esp_app_format
)