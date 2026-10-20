#include "thingsboard.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "app_events.h"
#include "ldr_sensor.h"
#if CONFIG_TB_HTTP_TLS
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "tb_http";

#define TB_HTTP_TASK_STACK  8192
#define TB_HTTP_TASK_PRIO   4
#if CONFIG_TB_HTTP_TLS
#define TB_HTTP_SCHEME "https"
#else
#define TB_HTTP_SCHEME "http"
#endif

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void send_telemetry(const app_light_data_t *d)
{
    char url[192];
    char payload[64];
    snprintf(url, sizeof(url), "http://eu.thingsboard.cloud/api/v1/2xbFDpWdIJlvoy74eel5/telemetry");
    int len = snprintf(payload, sizeof(payload), "{\"adc_raw\":%d,\"franja\":%d}", d->raw, d->franja);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 5000,
#if CONFIG_TB_HTTP_TLS
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el cliente HTTP");
        return;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "POST %s -> HTTP %d", payload, esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST fallido: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_post_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(thingsboard_get_period_ms()));        
        app_light_data_t d;
        if (ldr_sensor_read(&d) == ESP_OK) {
            send_telemetry(&d);
        }
    }
}

esp_err_t thingsboard_http_init(void)
{
    ESP_LOGI(TAG, "Envio HTTP a %s://eu.thingsboard.cloud iniciado", TB_HTTP_SCHEME);
    return (xTaskCreate(http_post_task, "tb_http_task", TB_HTTP_TASK_STACK, NULL, TB_HTTP_TASK_PRIO, NULL) == pdPASS) ? ESP_OK : ESP_FAIL;
}
