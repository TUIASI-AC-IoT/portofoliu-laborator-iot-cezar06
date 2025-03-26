/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "esp_http_server.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "soft-ap.h"
#include "http-server.h"
#include "wifi-scan.h"

#include "../mdns/include/mdns.h"
#include "button_monitor.h"

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void connect_wifi(void);
static void scan_mdns_services(void);

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
#define MAXIMUM_RETRY 5

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
  {
    esp_wifi_connect();
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    if (s_retry_num < MAXIMUM_RETRY)
    {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI("main", "retry to connect to the AP");
    }
    else
    {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI("main", "connect to the AP fail");
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI("main", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

static void connect_wifi(void)
{
  s_wifi_event_group = xEventGroupCreate();

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      &instance_got_ip));

  nvs_handle_t nvs_handle;
  char ssid[32];
  char pass[64];
  size_t ssid_len = sizeof(ssid);
  size_t pass_len = sizeof(pass);

  esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
  if (err != ESP_OK)
  {
    ESP_LOGE("main", "Error opening NVS handle");
    return;
  }

  err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
  if (err != ESP_OK)
  {
    ESP_LOGE("main", "Error reading SSID from NVS");
    nvs_close(nvs_handle);
    return;
  }

  err = nvs_get_str(nvs_handle, "pass", pass, &pass_len);
  if (err != ESP_OK)
  {
    ESP_LOGE("main", "Error reading password from NVS");
    nvs_close(nvs_handle);
    return;
  }

  nvs_close(nvs_handle);

  wifi_config_t wifi_config = {
      .sta = {
          .threshold.authmode = WIFI_AUTH_WPA2_PSK,
      },
  };
  memcpy(wifi_config.sta.ssid, ssid, ssid_len);
  memcpy(wifi_config.sta.password, pass, pass_len);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI("main", "wifi_init_sta finished.");

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT)
  {
    ESP_LOGI("main", "connected to ap SSID:%s", ssid);
  }
  else if (bits & WIFI_FAIL_BIT)
  {
    ESP_LOGI("main", "Failed to connect to SSID:%s", ssid);
  }
  else
  {
    ESP_LOGE("main", "UNEXPECTED EVENT");
  }
}

static void scan_mdns_services(void)
{
  ESP_ERROR_CHECK(mdns_init());

  ESP_LOGI("main", "Browsing for _http._tcp.local services...");
  mdns_result_t *results = NULL;
  esp_err_t err = mdns_query_ptr("_http", "_tcp", 3000, 20, &results);
  if (err)
  {
    ESP_LOGE("main", "mDNS Query Failed");
    return;
  }
  if (!results)
  {
    ESP_LOGW("main", "No mDNS services found!");
    return;
  }

  mdns_result_t *r = results;
  while (r)
  {
    ESP_LOGI("main", "Host %s", r->hostname);
    if (r->instance_name)
    {
      ESP_LOGI("main", "  Instance: %s", r->instance_name);
    }
    if (r->port)
    {
      ESP_LOGI("main", "  Port: %d", r->port);
    }
    if (r->txt_count)
    {
      ESP_LOGI("main", "  TXT records:");
      for (int t = 0; t < r->txt_count; t++)
      {
        ESP_LOGI("main", "    %s: %s", r->txt[t].key, r->txt[t].value);
      }
    }
    r = r->next;
  }

  mdns_query_results_free(results);
}

static bool check_wifi_credentials(void)
{
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
  if (err != ESP_OK)
  {
    return false;
  }

  size_t ssid_len, pass_len;
  err = nvs_get_str(nvs_handle, "ssid", NULL, &ssid_len);
  if (err != ESP_OK)
  {
    nvs_close(nvs_handle);
    return false;
  }

  err = nvs_get_str(nvs_handle, "pass", NULL, &pass_len);
  if (err != ESP_OK)
  {
    nvs_close(nvs_handle);
    return false;
  }

  nvs_close(nvs_handle);
  return true;
}

void app_main(void)
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  xTaskCreate(button_monitor_task, "button_monitor", 2048, NULL, 5, NULL);

  if (check_wifi_credentials())
  {
    ESP_LOGI("main", "Starting normal application mode");

    connect_wifi();

    scan_mdns_services();

    while (1)
    {
      scan_mdns_services();
      vTaskDelay(pdMS_TO_TICKS(30000)); 
    }
  }
  else
  {
    ESP_LOGI("main", "Starting provisioning mode");

    wifi_scan_result_t *scan_result = wifi_start_scan();
    if (scan_result == NULL)
    {
      ESP_LOGE("main", "WiFi scan failed!");
      return;
    }

    set_scan_results(scan_result);

    wifi_init_softap();

    esp_err_t err = mdns_init();
    if (err == ESP_OK)
    {
      mdns_hostname_set("esp32-config");
      mdns_instance_name_set("ESP32 Web Configuration");
      mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }

    httpd_handle_t server = start_webserver();
    if (server == NULL)
    {
      ESP_LOGE("main", "Error starting web server!");
      return;
    }
  }
}