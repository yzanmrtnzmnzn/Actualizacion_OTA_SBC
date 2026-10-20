#include "web_portal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "config_store.h"

static const char *TAG = "web_portal";

#define HTML_BUF_SIZE 1536

static void url_decode(char *s)
{
    char *out = s;
    while (*s) {
        if (*s == '+') {
            *out++ = ' ';
            s++;
        } else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], '\0' };
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_config = { 0 };
    esp_wifi_scan_start(&scan_config, true);
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    wifi_ap_record_t *ap_list = (ap_count > 0) ? calloc(ap_count, sizeof(wifi_ap_record_t)) : NULL;
    if (ap_list != NULL) {
        esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    } else {
        ap_count = 0;
    }

    char *html = malloc(HTML_BUF_SIZE);
    if (html == NULL) {
        free(ap_list);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Sin memoria disponible");
        return ESP_FAIL;
    }

    int len = snprintf(html, HTML_BUF_SIZE,
            "<html><body><h1>Configuracion WiFi</h1>"
            "<form action='/save' method='post'>"
            "Red: <select name='ssid'>");

    if (ap_count == 0) {
        len += snprintf(html + len, HTML_BUF_SIZE - len,
                        "<option value=''>-- sin redes, reintenta --</option>");
    } else {
        for (int i = 0; i < ap_count && len < HTML_BUF_SIZE - 96; i++) {
            len += snprintf(html + len, HTML_BUF_SIZE - len,
                            "<option value='%s'>%s (%d dBm)</option>",
                            (char *)ap_list[i].ssid, (char *)ap_list[i].ssid, ap_list[i].rssi);
        }
    }
    free(ap_list);

    snprintf(html + len, HTML_BUF_SIZE - len,
             "</select><br>"
             "Contrasena: <input type='password' name='pwd' maxlength='63'><br>"
             "<input type='submit' value='Conectar'>"
             "</form></body></html>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    free(html);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[257];
    int received = 0;
    if (req->content_len <= 0 || req->content_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Peticion invalida");
        return ESP_FAIL;
    }
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error leyendo la peticion");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';

    char ssid[CFG_SSID_MAX_LEN] = { 0 };
    char pwd[CFG_PASS_MAX_LEN]  = { 0 };
    httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(buf, "pwd", pwd, sizeof(pwd));
    url_decode(ssid);
    url_decode(pwd);

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Debes seleccionar una red valida");
        return ESP_FAIL;
    }
    if (config_store_save(ssid, pwd) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudo guardar la configuracion");
        return ESP_FAIL;
    }

    httpd_resp_send(req, "Guardado", HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "Credenciales recibidas para SSID '%s'. Reiniciando...", ssid);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t not_found_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_portal_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar el servidor web");
        return err;
    }

    httpd_uri_t root_uri = { .uri = "/",     .method = HTTP_GET,  .handler = root_get_handler };
    httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &save_uri);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found_handler);

    ESP_LOGI(TAG, "Servidor web de configuracion iniciado");
    return ESP_OK;
}
