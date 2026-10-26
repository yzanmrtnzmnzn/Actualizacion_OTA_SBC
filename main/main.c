#include "esp_log.h"
#include "esp_err.h"

#include "config_store.h"
#include "wifi_manager.h"
#include "web_portal.h"

#include "app_events.h"
#include "ldr_sensor.h"
//#include "bcd_display.h"
//#include "oled_display.h"
#include "thingsboard.h"
#include "ota_update.h"

static const char *TAG = "RETO4";

// Modo cliente WiFi
static void start_station_mode(void)//inicializar actuadores y displays ANTES de usar los sensores
{
    ESP_ERROR_CHECK(app_events_init());

    //ESP_ERROR_CHECK(oled_display_init());
    //ESP_ERROR_CHECK(bcd_display_init());

    // Ahorro energetico: el sensor solo se configura aqui; el ADC se lee bajo demanda
    // desde los emisores MQTT/HTTP. Se inicializa antes que ellos porque son quienes lo leen.
    ESP_ERROR_CHECK(ldr_sensor_init());

    ESP_ERROR_CHECK(thingsboard_mqtt_init());
    ESP_ERROR_CHECK(thingsboard_http_init());
}

// Modo configuracion Access Point
static void start_portal_mode(void)
{
    ESP_ERROR_CHECK(wifi_manager_start_ap());
    ESP_ERROR_CHECK(web_portal_start());
}

void app_main(void)
{
    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(wifi_manager_init());

    if (wifi_manager_connect_saved()) {
        ESP_LOGI(TAG, "WiFi conectado. Arrancando monitorizacion (modo cliente)");
        start_station_mode();
    } else {
        ESP_LOGW(TAG, "Sin conexion WiFi. Arrancando Access Point + portal de configuracion");
        start_portal_mode();
    }
}
