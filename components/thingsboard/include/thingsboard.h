#pragma once
#include "esp_err.h"

esp_err_t thingsboard_mqtt_init(void);
esp_err_t thingsboard_http_init(void);

int  thingsboard_get_period_ms(void);
void thingsboard_set_period_ms(int ms);