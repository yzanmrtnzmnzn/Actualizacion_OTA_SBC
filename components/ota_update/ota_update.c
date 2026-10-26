#include "ota_update.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"

static const char *TAG = "OTA";

#define OTA_VERSION_URL \
    "https://github.com/yzanmrtnzmnzn/Actualizacion_OTA_SBC/releases/latest/download/version.txt"

#define OTA_FIRMWARE_URL \
    "https://github.com/yzanmrtnzmnzn/Actualizacion_OTA_SBC/latest/download/firmware.bin"

#define OTA_VERSION_BUFFER_SIZE 32

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} response_buffer_t;


/* Recibe los datos de la respuesta HTTP. */
static esp_err_t version_http_event_handler(
    esp_http_client_event_t *event)
{
    response_buffer_t *response =
        (response_buffer_t *)event->user_data;

    if (event->event_id == HTTP_EVENT_ON_DATA &&
        response != NULL &&
        event->data_len > 0) {

        size_t received = (size_t)event->data_len;

        if (response->length >= response->capacity ||
            received >= response->capacity - response->length) {
            return ESP_FAIL;
        }

        memcpy(response->buffer + response->length,
               event->data,
               received);

        response->length += received;
        response->buffer[response->length] = '\0';
    }

    return ESP_OK;
}


/* Descarga el texto que contiene la última versión publicada. */
static esp_err_t get_latest_version(char *version, size_t size)
{
    if (version == NULL || size < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    version[0] = '\0';

    response_buffer_t response = {
        .buffer = version,
        .capacity = size,
        .length = 0
    };

    esp_http_client_config_t http_config = {
        .url = OTA_VERSION_URL,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = version_http_event_handler,
        .user_data = &response
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&http_config);

    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error descargando la version: %s",
                 esp_err_to_name(err));
        return err;
    }

    if (status != 200 || response.length == 0) {
        ESP_LOGE(TAG, "Respuesta HTTP no valida: %d", status);
        return ESP_FAIL;
    }

    /* Elimina saltos de línea y espacios finales. */
    while (response.length > 0 &&
           isspace((unsigned char)version[response.length - 1])) {
        version[--response.length] = '\0';
    }

    if (response.length == 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}


/* Acepta versiones numéricas con formato mayor.menor.parche. */
static bool parse_version(const char *text,
                          unsigned int *major,
                          unsigned int *minor,
                          unsigned int *patch)
{
    char extra;

    return sscanf(text, "%u.%u.%u%c",
                  major, minor, patch, &extra) == 3;
}


/* Comprueba si la versión remota es posterior a la instalada. */
static bool is_newer_version(const char *remote,
                             const char *local)
{
    unsigned int r_major, r_minor, r_patch;
    unsigned int l_major, l_minor, l_patch;

    if (!parse_version(remote, &r_major, &r_minor, &r_patch) ||
        !parse_version(local, &l_major, &l_minor, &l_patch)) {
        ESP_LOGE(TAG, "Formato de version incorrecto");
        return false;
    }

    if (r_major != l_major) {
        return r_major > l_major;
    }

    if (r_minor != l_minor) {
        return r_minor > l_minor;
    }

    return r_patch > l_patch;
}


/* Comprueba la versión y actualiza el firmware si es necesario. */
esp_err_t ota_check_and_update(void)
{
    const esp_app_desc_t *current_app = esp_app_get_description();

    char latest_version[OTA_VERSION_BUFFER_SIZE] = {0};

    ESP_LOGI(TAG, "Comprobando la version de GitHub");

    esp_err_t err = get_latest_version(
        latest_version, sizeof(latest_version));

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo comprobar la version");
        return err;
    }

    ESP_LOGI(TAG, "Version instalada: %s", current_app->version);
    ESP_LOGI(TAG, "Version publicada: %s", latest_version);

    if (!is_newer_version(latest_version, current_app->version)) {
        ESP_LOGI(TAG, "No hay una actualizacion posterior");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Descargando el nuevo firmware");

    esp_http_client_config_t http_config = {
        .url = OTA_FIRMWARE_URL,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config
    };

    err = esp_https_ota(&ota_config);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo OTA: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA completada. Reiniciando...");
    esp_restart();

    return ESP_OK;
}