#pragma once
#include <stddef.h>
#include "esp_err.h"

#define CFG_SSID_MAX_LEN 32
#define CFG_PASS_MAX_LEN 64

esp_err_t config_store_init(void);
esp_err_t config_store_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t config_store_save(const char *ssid, const char *pass);