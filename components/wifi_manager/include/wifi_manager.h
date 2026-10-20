#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t wifi_manager_init(void);

bool wifi_manager_connect_saved(void);//True si se conecta a una red
esp_err_t wifi_manager_start_ap(void);