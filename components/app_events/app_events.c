#include "app_events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

ESP_EVENT_DEFINE_BASE(APP_EVENTS);

static esp_event_loop_handle_t s_loop = NULL;

esp_err_t app_events_init(void)
{
    if (s_loop != NULL) {
        return ESP_OK;
    }
    const esp_event_loop_args_t args = {
        .queue_size      = 16,
        .task_name       = "app_events",
        .task_priority   = 4,
        .task_stack_size = 4096,
        .task_core_id    = tskNO_AFFINITY,
    };
    return esp_event_loop_create(&args, &s_loop);
}

esp_err_t app_events_post(app_event_id_t id, const void *data, size_t size)
{
    if (s_loop == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_post_to(s_loop, APP_EVENTS, id, data, size, pdMS_TO_TICKS(100));
}

esp_err_t app_events_subscribe(app_event_id_t id, esp_event_handler_t handler, void *arg)
{
    if (s_loop == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_handler_register_with(s_loop, APP_EVENTS, id, handler, arg);
}
