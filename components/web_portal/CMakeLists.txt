idf_component_register(SRCS "web_portal.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_http_server esp_wifi config_store)
