#include "ldr_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "ldr_sensor";

#define LDR_UNIT        ADC_UNIT_1
#define LDR_CHANNEL     ADC_CHANNEL_6   /* GPIO34 */
#define LDR_LOCK_MS     100

static adc_oneshot_unit_handle_t s_adc = NULL;
static SemaphoreHandle_t s_lock = NULL;  

esp_err_t ldr_sensor_init(void)
{
    if (s_adc != NULL) {
        return ESP_OK;   
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = LDR_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc));

    adc_oneshot_chan_cfg_t chan_config = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, LDR_CHANNEL, &chan_config));

    ESP_LOGI(TAG, "LDR listo. El ADC solo se leera bajo demanda (justo antes de enviar)");
    return ESP_OK;
}

esp_err_t ldr_sensor_read(app_light_data_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_adc == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LDR_LOCK_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc, LDR_CHANNEL, &raw);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Fallo al leer el ADC (%s)", esp_err_to_name(err));
        return err;
    }

    int franja = (raw * 99) / 4095;
    if (franja > 99) franja = 99;
    if (franja < 0)  franja = 0;

    out->raw    = raw;
    out->franja = franja;
    ESP_LOGI(TAG, "ADC: %d | Franja: %02d", raw, franja);

    app_events_post(APP_EVT_LIGHT_UPDATE, out, sizeof(*out));
    return ESP_OK;
}
