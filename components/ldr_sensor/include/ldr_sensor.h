#pragma once
#include "esp_err.h"
#include "app_events.h"   /* app_light_data_t */

/* Configura el ADC. No crea ninguna tarea ni realiza lecturas. */
esp_err_t ldr_sensor_init(void);

/* Realiza UNA lectura del ADC en este momento. Llamar solo cuando se vaya a enviar el dato. */
esp_err_t ldr_sensor_read(app_light_data_t *out);
