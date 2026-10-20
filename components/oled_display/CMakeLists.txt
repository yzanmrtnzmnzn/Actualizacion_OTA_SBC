idf_component_register(SRCS "oled_display.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES ssd1306 app_events)
