#include "config_store.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config_store";

#define WIFI_CFG_NAMESPACE "wifi_cfg"
#define WIFI_CFG_KEY       "config"
#define WIFI_CFG_MAX_LEN   200  

esp_err_t config_store_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t config_store_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CFG_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Aun no hay configuracion guardada (primer arranque)");
        return err;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo abrir NVS para lectura (%s)", esp_err_to_name(err));
        return err;
    }

    char raw[WIFI_CFG_MAX_LEN];
    size_t len = sizeof(raw);
    err = nvs_get_str(handle, WIFI_CFG_KEY, raw, &len);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo leer la configuracion de NVS (%s)", esp_err_to_name(err));
        return err;
    }

    ssid[0] = '\0';
    pass[0] = '\0';
    char *line = strtok(raw, "\r\n");
    while (line != NULL) {
        if (strncmp(line, "SSID=", 5) == 0) {
            strlcpy(ssid, line + 5, ssid_len);
        } else if (strncmp(line, "PASSWORD=", 9) == 0) {
            strlcpy(pass, line + 9, pass_len);
        }
        line = strtok(NULL, "\r\n");
    }
    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG, "La configuracion guardada no contiene un SSID valido");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t config_store_save(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CFG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo abrir NVS para escritura (%s)", esp_err_to_name(err));
        return err;
    }

    char buf[WIFI_CFG_MAX_LEN];
    int len = snprintf(buf, sizeof(buf), "SSID=%s\nPASSWORD=%s\n", ssid, pass ? pass : "");
    if (len < 0 || len >= (int)sizeof(buf)) {
        ESP_LOGE(TAG, "Las nuevas credenciales no caben en el buffer");
        nvs_close(handle);
        return ESP_FAIL;
    }

    err = nvs_set_str(handle, WIFI_CFG_KEY, buf);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Nuevas credenciales guardadas en NVS (SSID=%s)", ssid);
    } else {
        ESP_LOGE(TAG, "No se pudieron guardar las credenciales (%s)", esp_err_to_name(err));
    }
    return err;
}
