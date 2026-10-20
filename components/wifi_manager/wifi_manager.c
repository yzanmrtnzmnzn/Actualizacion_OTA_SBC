#include "wifi_manager.h"
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "config_store.h"

static const char *TAG = "wifi_manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_retry_num = 0;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_STA_MAX_RETRY) {
            s_retry_num++;
            ESP_LOGI(TAG, "Reintentando conexion (%d/%d)...", s_retry_num, CONFIG_STA_MAX_RETRY);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_wifi_connect();
        } else if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "IP obtenida: " IPSTR, IP2STR(&event->ip_info.ip));
        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "Cliente conectado al AP, MAC:" MACSTR, MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "Cliente desconectado del AP, MAC:" MACSTR, MAC2STR(event->mac));
    }
}

esp_err_t wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    return ESP_OK;
}

static bool wifi_connect_sta(const char *ssid, const char *password, uint32_t timeout_ms)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGW(TAG, "SSID vacio, no se puede conectar");
        return false;
    }

    s_wifi_event_group = xEventGroupCreate();
    s_retry_num = 0;

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password ? password : "", sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode =
        (password == NULL || strlen(password) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Conectando a SSID '%s'...", ssid);

    TickType_t ticks_to_wait = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, ticks_to_wait);
    bool connected = (bits & WIFI_CONNECTED_BIT) != 0;

    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;

    if (!connected) {
        ESP_LOGW(TAG, "No se pudo conectar a '%s'", ssid);
        esp_wifi_disconnect();
        esp_wifi_stop();
    } else {
        ESP_LOGI(TAG, "Conectado correctamente a '%s'", ssid);
    }
    return connected;
}

bool wifi_manager_connect_saved(void)
{
    char ssid[CFG_SSID_MAX_LEN] = { 0 };
    char pass[CFG_PASS_MAX_LEN] = { 0 };

    if (config_store_load(ssid, sizeof(ssid), pass, sizeof(pass)) != ESP_OK) {
        ESP_LOGW(TAG, "No hay credenciales guardadas validas");
        return false;
    }
    ESP_LOGI(TAG, "Credenciales leidas de la configuracion guardada: SSID='%s'", ssid);
    return wifi_connect_sta(ssid, pass, CONFIG_CONNECT_TIMEOUT_MS);
}

static void wifi_get_ap_ssid(char *out, size_t out_len)
{
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(out, out_len, "esp%02x%02x", mac[4], mac[5]);
}

esp_err_t wifi_manager_start_ap(void)
{
    char ap_ssid[33];
    wifi_get_ap_ssid(ap_ssid, sizeof(ap_ssid));

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.ap.ssid, ap_ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(ap_ssid);
    strlcpy((char *)wifi_config.ap.password, CONFIG_AP_PASSWORD, sizeof(wifi_config.ap.password));
    wifi_config.ap.max_connection = CONFIG_AP_MAX_STA_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.ap.channel = CONFIG_AP_CHANNEL;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Modo Access Point iniciado. SSID: '%s'  Password: '%s'", ap_ssid, CONFIG_AP_PASSWORD);
    return ESP_OK;
}
