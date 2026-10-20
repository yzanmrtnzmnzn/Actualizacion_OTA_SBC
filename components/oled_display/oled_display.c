#include "oled_display.h"
#include <stdio.h>
#include <string.h>
#include "ssd1306.h"
#include "app_events.h"

#define PIN_SDA   21
#define PIN_SCL   22
#define PIN_RESET -1

static SSD1306_t s_oled;

static void on_light(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    const app_light_data_t *d = (const app_light_data_t *)event_data;
    char linea1[24];
    char linea2[24];
    char barra[24];

    snprintf(linea1, sizeof(linea1), "ADC: %4d", d->raw);
    snprintf(linea2, sizeof(linea2), "Franja: %02d", d->franja);

    int filled = d->franja / 10;
    if (filled > 10) filled = 10;
    if (filled < 0)  filled = 0;
    int pos = 0;
    barra[pos++] = '[';
    for (int i = 0; i < 10; i++) {
        barra[pos++] = (i < filled) ? '#' : '-';
    }
    barra[pos++] = ']';
    barra[pos] = '\0';

    ssd1306_display_text(&s_oled, 2, linea1, strlen(linea1), false);
    ssd1306_display_text(&s_oled, 4, linea2, strlen(linea2), false);
    ssd1306_display_text(&s_oled, 6, barra, strlen(barra), false);
}

esp_err_t oled_display_init(void)
{
    i2c_master_init(&s_oled, PIN_SDA, PIN_SCL, PIN_RESET);
    ssd1306_init(&s_oled, 128, 64);
    ssd1306_clear_screen(&s_oled, false);
    ssd1306_contrast(&s_oled, 0xff);
    ssd1306_display_text(&s_oled, 0, "Monitor LDR - WiFi OK", 21, false);
    return app_events_subscribe(APP_EVT_LIGHT_UPDATE, on_light, NULL);
}
