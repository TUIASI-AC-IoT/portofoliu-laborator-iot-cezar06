#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "freertos/event_groups.h"

#include "esp_http_server.h"
#include "wifi-scan.h"

static const char *TAG = "http-server";
static wifi_scan_result_t *scan_results = NULL;

void set_scan_results(wifi_scan_result_t *results)
{
    scan_results = results;
}

/* Our URI handler function to be called during GET /uri request */
esp_err_t get_handler(httpd_req_t *req)
{
    const char *resp_template_start =
        "<html><body>"
        "<h2>ESP32 WiFi Configuration</h2>"
        "<form action=\"/results.html\" method=\"post\">"
        "<label for=\"ssid\">Available Networks:</label><br>"
        "<select name=\"ssid\">";

    const char *resp_template_end =
        "</select><br><br>"
        "<label for=\"ipass\">Security key:</label><br>"
        "<input type=\"password\" name=\"ipass\"><br><br>"
        "<input type=\"submit\" value=\"Connect\">"
        "</form></body></html>";

    httpd_resp_set_type(req, "text/html");

    httpd_resp_send_chunk(req, resp_template_start, strlen(resp_template_start));

    if (scan_results != NULL && scan_results->count > 0)
    {
        char option[128];
        for (int i = 0; i < scan_results->count; i++)
        {
            snprintf(option, sizeof(option),
                     "<option value=\"%s\">%s (RSSI: %d)</option>",
                     scan_results->ap_info[i].ssid,
                     scan_results->ap_info[i].ssid,
                     scan_results->ap_info[i].rssi);
            httpd_resp_send_chunk(req, option, strlen(option));
        }
    }
    else
    {
        const char *no_networks = "<option value=\"\">No networks found</option>";
        httpd_resp_send_chunk(req, no_networks, strlen(no_networks));
    }

    // Send the end of the HTML
    httpd_resp_send_chunk(req, resp_template_end, strlen(resp_template_end));

    // Send empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Our URI handler function to be called during POST /uri request */
esp_err_t post_handler(httpd_req_t *req)
{
    char content[100];
    char ssid[32];
    char password[64];
    char response[256];

    // Read POST data
    int ret = httpd_req_recv(req, content, MIN(req->content_len, sizeof(content) - 1));
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';

    // Parse SSID and password from POST data
    char *ssid_start = strstr(content, "ssid=");
    char *pass_start = strstr(content, "ipass=");

    if (ssid_start && pass_start)
    {
        ssid_start += 5; // Skip "ssid="
        pass_start += 6; // Skip "ipass="

        char *ssid_end = strchr(ssid_start, '&');
        if (ssid_end)
        {
            strncpy(ssid, ssid_start, ssid_end - ssid_start);
            ssid[ssid_end - ssid_start] = '\0';
        }

        strncpy(password, pass_start, sizeof(password) - 1);
        password[sizeof(password) - 1] = '\0';

        // Store credentials in NVS
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK)
        {
            err = nvs_set_str(nvs_handle, "ssid", ssid);
            if (err == ESP_OK)
            {
                err = nvs_set_str(nvs_handle, "pass", password);
                if (err == ESP_OK)
                {
                    nvs_commit(nvs_handle);
                }
            }
            nvs_close(nvs_handle);
        }

        // Create response
        snprintf(response, sizeof(response),
                 "<html><body>"
                 "<h2>Configuration Saved</h2>"
                 "<p>SSID: %s</p>"
                 "<p>Password: %s</p>"
                 "<p>The device will restart and attempt to connect to this network.</p>"
                 "</body></html>",
                 ssid, "********");

        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, response, strlen(response));

        // Delay briefly to allow response to be sent
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Restart ESP32
        esp_restart();
        return ESP_OK;
    }
    else
    {
        snprintf(response, sizeof(response),
                 "<html><body>"
                 "<h2>Error</h2>"
                 "<p>Invalid form data received.</p>"
                 "</body></html>");
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

/* URI handler structure for GET /uri */
httpd_uri_t uri_get = {
    .uri = "/index.html",
    .method = HTTP_GET,
    .handler = get_handler,
    .user_ctx = NULL};

/* URI handler structure for POST /uri */
httpd_uri_t uri_post = {
    .uri = "/results.html",
    .method = HTTP_POST,
    .handler = post_handler,
    .user_ctx = NULL};

/* Function for starting the webserver */
httpd_handle_t start_webserver(void)
{
    /* Generate default configuration */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Empty handle to esp_http_server */
    httpd_handle_t server = NULL;

    /* Start the httpd server */
    if (httpd_start(&server, &config) == ESP_OK)
    {
        /* Register URI handlers */
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);
        ESP_LOGI(TAG, "Web server started successfully");
    }
    /* If server failed to start, handle will be NULL */
    return server;
}

/* Function for stopping the webserver */
void stop_webserver(httpd_handle_t server)
{
    if (server)
    {
        /* Stop the httpd server */
        httpd_stop(server);
    }
}