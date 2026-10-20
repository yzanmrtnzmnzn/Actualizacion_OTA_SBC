#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(APP_EVENTS);

typedef enum {
    APP_EVT_LIGHT_UPDATE = 0,   
} app_event_id_t; //lista con los sensores q queramos usar

typedef struct {
    int raw;      
    int franja;  
} app_light_data_t;

esp_err_t app_events_init(void);
esp_err_t app_events_post(app_event_id_t id, const void *data, size_t size);
esp_err_t app_events_subscribe(app_event_id_t id, esp_event_handler_t handler, void *arg);
