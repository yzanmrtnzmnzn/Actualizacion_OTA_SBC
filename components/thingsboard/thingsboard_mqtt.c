#include "thingsboard.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "app_events.h"
#include "ldr_sensor.h"
#include <stdlib.h>
#include "cJSON.h"
#include "ota_update.h"
#include "nvs.h"

static const char *TAG = "tb_mqtt";

#define TB_TELEMETRY_TOPIC  "v1/devices/me/telemetry"
#define TB_MQTT_TASK_STACK  4096
#define TB_MQTT_TASK_PRIO   4

#define OTA_AUTO_TASK_STACK          8192
#define OTA_INTERVAL_DEFAULT_MINUTES 60
#define OTA_INTERVAL_MIN_MINUTES     1
#define OTA_INTERVAL_MAX_MINUTES     1440

static char s_uri[96];
static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;
static volatile int s_period_ms = CONFIG_TB_PERIOD_MS;
static volatile bool s_ota_in_progress = false;
static volatile int s_ota_interval_minutes = OTA_INTERVAL_DEFAULT_MINUTES;
static TaskHandle_t s_ota_auto_task_handle = NULL;
static portMUX_TYPE s_ota_lock = portMUX_INITIALIZER_UNLOCKED;

int thingsboard_get_period_ms(void)
{
    return s_period_ms;
}

void thingsboard_set_period_ms(int ms)
{
    s_period_ms = ms;
}



static void load_ota_interval(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("tb_config", NVS_READONLY, &handle);

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Usando intervalo OTA por defecto: %d minutos",
                 OTA_INTERVAL_DEFAULT_MINUTES);
        return;
    }

    uint32_t minutes = OTA_INTERVAL_DEFAULT_MINUTES;

    err = nvs_get_u32(handle, "ota_minutes", &minutes);
    nvs_close(handle);

    if (err == ESP_OK &&
        minutes >= OTA_INTERVAL_MIN_MINUTES &&
        minutes <= OTA_INTERVAL_MAX_MINUTES) {

        s_ota_interval_minutes = (int)minutes;
    }

    ESP_LOGI(TAG, "Intervalo OTA configurado: %d minutos",
             s_ota_interval_minutes);
}


static esp_err_t save_ota_interval(uint32_t minutes)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open("tb_config", NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(handle, "ota_minutes", minutes);

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}


static bool ota_try_start(void)
{
    bool can_start = false;

    portENTER_CRITICAL(&s_ota_lock);

    if (!s_ota_in_progress) {
        s_ota_in_progress = true;
        can_start = true;
    }

    portEXIT_CRITICAL(&s_ota_lock);

    return can_start;
}


static void ota_finish(void)
{
    portENTER_CRITICAL(&s_ota_lock);
    s_ota_in_progress = false;
    portEXIT_CRITICAL(&s_ota_lock);
}

static void ota_update_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Iniciando comprobacion de actualizacion OTA");

    esp_err_t err = ota_check_and_update();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error en la OTA: %s",
                 esp_err_to_name(err));
    }
    ota_finish();

    vTaskDelete(NULL);
}

static void ota_auto_check_task(void *arg)
{
    (void)arg;

    while (1) {

        int interval_minutes = s_ota_interval_minutes;

        uint32_t wait_ms =
            (uint32_t)interval_minutes * 60U * 1000U;

        ESP_LOGI(TAG,
                 "Proxima comprobacion OTA en %d minutos",
                 interval_minutes);
        uint32_t notification = ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(wait_ms)
        );

        if (notification > 0) {
            ESP_LOGI(TAG,
                     "Intervalo OTA modificado; reiniciando la espera");
            continue;
        }

        if (!ota_try_start()) {
            ESP_LOGW(TAG,
                     "Comprobacion OTA omitida: ya hay una OTA en curso");
            continue;
        }

        ESP_LOGI(TAG, "Iniciando comprobacion OTA automatica");

        esp_err_t err = ota_check_and_update();

        if (err != ESP_OK) {
            ESP_LOGE(TAG,
                     "Error en la comprobacion OTA automatica: %s",
                     esp_err_to_name(err));
        }

        ota_finish();
    }
}
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        s_connected = true;
        
        esp_mqtt_client_subscribe(
        event->client,
        "v1/devices/me/rpc/request/+",
        1
        );
    
        break;
    case MQTT_EVENT_SUBSCRIBED:
    	ESP_LOGI(TAG, "Suscripcion MQTT confirmada, msg_id=%d",
             event->msg_id);
    	break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        s_connected = false;
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(TAG, "Last errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "Conexion rechazada (0x%x): revisa el access token", event->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    
	case MQTT_EVENT_DATA: 
		ESP_LOGI(TAG, "RPC recibido en topic: %.*s", event->topic_len, event->topic);
         {
	    char *payload = malloc(event->data_len + 1);
	
	    if (payload == NULL) {
	        ESP_LOGE(TAG, "Sin memoria para RPC");
	        break;
	    }
	
	    memcpy(payload, event->data, event->data_len);
	    payload[event->data_len] = '\0';
	
	    ESP_LOGI(TAG, "RPC recibido: %s", payload);
	
	    cJSON *root = cJSON_Parse(payload);
	
	    if (root != NULL) {
			cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
			cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
			cJSON *value = NULL;
			
			if (cJSON_IsObject(params)) {
			    value = cJSON_GetObjectItemCaseSensitive(params, "value");
			}
			
			ESP_LOGI(TAG, "RPC: params_type=%d, value_type=%d, value=%d",
			         params != NULL ? params->type : -1,
			         value != NULL ? value->type : -1,
			         cJSON_IsNumber(value) ? value->valueint : -1);
	
			if (cJSON_IsString(method) &&
			    strcmp(method->valuestring, "setInterval") == 0) {
			
			    if (cJSON_IsNumber(value)) {
			
			        int new_period = value->valueint;
			
			        if (new_period >= 1000 && new_period <= 60000) {
			            thingsboard_set_period_ms(new_period);
			
			            ESP_LOGI(TAG, "Nuevo intervalo de telemetria: %d ms",
			                     new_period);
			        } else {
			            ESP_LOGW(TAG, "Intervalo de telemetria fuera de rango: %d",
			                     new_period);
			        }
			
			    } else {
			        ESP_LOGW(TAG,
			                 "setInterval necesita un parametro value numerico");
			    }
			
			} else if (cJSON_IsString(method) &&
			           strcmp(method->valuestring, "setOtaInterval") == 0) {
			
			    if (cJSON_IsNumber(value)) {
			
			        int new_interval = value->valueint;
			
			        if (new_interval >= OTA_INTERVAL_MIN_MINUTES &&
			            new_interval <= OTA_INTERVAL_MAX_MINUTES) {
			
			            s_ota_interval_minutes = new_interval;
			
			            esp_err_t save_err =
			                save_ota_interval((uint32_t)new_interval);
			
			            if (save_err != ESP_OK) {
			                ESP_LOGE(TAG,
			                         "No se pudo guardar el intervalo OTA: %s",
			                         esp_err_to_name(save_err));
			            }
			
			            
			            if (s_ota_auto_task_handle != NULL) {
			                xTaskNotifyGive(s_ota_auto_task_handle);
			            }
			
			            ESP_LOGI(TAG,
			                     "Nuevo intervalo de comprobacion OTA: %d minutos",
			                     new_interval);
			
			        } else {
			            ESP_LOGW(TAG,
			                     "Intervalo OTA fuera de rango: %d minutos",
			                     new_interval);
			        }
			
			    } else {
			        ESP_LOGW(TAG,
			                 "setOtaInterval necesita un parametro value numerico");
			    }
			
			} else if (cJSON_IsString(method) &&
			           strcmp(method->valuestring, "startOta") == 0) {
			
			    if (!ota_try_start()) {
			
			        ESP_LOGW(TAG, "Ya hay una OTA en curso");
			
			    } else {
			
			        BaseType_t result = xTaskCreate(
			            ota_update_task,
			            "ota_update",
			            8192,
			            NULL,
			            5,
			            NULL
			        );
			
			        if (result != pdPASS) {
			            ota_finish();
			
			            ESP_LOGE(TAG, "No se pudo crear la tarea OTA");
			        } else {
			            ESP_LOGI(TAG, "Orden OTA recibida desde ThingsBoard");
			        }
			    }
			
			} else {
			    ESP_LOGW(TAG, "Metodo o parametros RPC no validos");
			}
	
	        cJSON_Delete(root);
	    } else {
	        ESP_LOGW(TAG, "JSON RPC no valido");
	    }
	
	    free(payload);
	    break;
	}

    default:
        ESP_LOGD(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_publish_task(void *arg)
{
    char payload[64];
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(thingsboard_get_period_ms()));
        if (!s_connected) {
            continue;
        }
        app_light_data_t d;
        if (ldr_sensor_read(&d) != ESP_OK) {
            continue;
        }
        int len = snprintf(payload, sizeof(payload), "{\"adc_raw\":%d,\"franja\":%d}", d.raw, d.franja);
        int msg_id = esp_mqtt_client_publish(s_client, TB_TELEMETRY_TOPIC, payload, len, 1, 0);
        ESP_LOGI(TAG, "Publicado %s (msg_id=%d)", payload, msg_id);
    }
}

esp_err_t thingsboard_mqtt_init(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://eu.thingsboard.cloud:1883",
        .credentials.username = "BPo25jddeDHbnJsezYZz",
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        return err;
    }

		ESP_LOGI(TAG, "Cliente MQTT iniciado");
		
		/* Tarea que envia la telemetria. */
		if (xTaskCreate(
		        mqtt_publish_task,
		        "tb_mqtt_task",
		        TB_MQTT_TASK_STACK,
		        NULL,
		        TB_MQTT_TASK_PRIO,
		        NULL
		    ) != pdPASS) {
		
		    ESP_LOGE(TAG, "No se pudo crear la tarea MQTT");
		    return ESP_FAIL;
		}
		
		/* Tarea que comprueba automaticamente si hay una nueva OTA. */
		if (xTaskCreate(
		        ota_auto_check_task,
		        "ota_auto_check",
		        OTA_AUTO_TASK_STACK,
		        NULL,
		        4,
		        &s_ota_auto_task_handle
		    ) != pdPASS) {
		
		    ESP_LOGE(TAG, "No se pudo crear la tarea de comprobacion OTA");
		    return ESP_FAIL;
		}
		
		ESP_LOGI(TAG, "Comprobacion OTA automatica activada");
		return ESP_OK;
}
