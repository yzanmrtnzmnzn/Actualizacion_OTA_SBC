#include "bcd_display.h"
#include "driver/gpio.h"
#include "app_events.h"

#define U_A 13
#define U_B 14
#define U_C 4
#define U_D 16
#define D_A 17
#define D_B 5
#define D_C 18
#define D_D 19

static const int BCD_UNITS[4] = { U_A, U_B, U_C, U_D };
static const int BCD_TENS[4]  = { D_A, D_B, D_C, D_D };

static void write_digit(int value, const int *pins)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(pins[i], (value >> i) & 0x01);
    }
}

static void on_light(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    const app_light_data_t *d = (const app_light_data_t *)event_data;
    write_digit(d->franja / 10, BCD_TENS);
    write_digit(d->franja % 10, BCD_UNITS);
}

esp_err_t bcd_display_init(void)
{
    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(BCD_UNITS[i]);
        gpio_set_direction(BCD_UNITS[i], GPIO_MODE_OUTPUT);
        gpio_reset_pin(BCD_TENS[i]);
        gpio_set_direction(BCD_TENS[i], GPIO_MODE_OUTPUT);
    }
    return app_events_subscribe(APP_EVT_LIGHT_UPDATE, on_light, NULL);
}
