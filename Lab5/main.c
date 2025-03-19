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

void app_main(void)
{
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // TODO: 3. SSID scanning in STA mode
  wifi_scan_result_t *scan_result = wifi_start_scan();
  if (scan_result == NULL)
  {
    ESP_LOGE("main", "WiFi scan failed!");
    return;
  }
  ESP_LOGI("main", "WiFi scan completed successfully");

  // Set scan results for web server to use
  set_scan_results(scan_result);

  // TODO: 1. Start the softAP mode
  wifi_init_softap();

  // TODO: 4. mDNS init (if there is time left)
  esp_err_t err = mdns_init();
  if (err)
  {
    ESP_LOGE("main", "mDNS Init failed: %d", err);
    return;
  }

  // Set mDNS hostname and instance name
  mdns_hostname_set("esp32-config");
  mdns_instance_name_set("ESP32 Web Configuration");

  mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

  ESP_LOGI("main", "mDNS initialized. Device can be accessed at esp32-config.local");

  // TODO: 2. Start the web server
  httpd_handle_t server = start_webserver();
  if (server == NULL)
  {
    ESP_LOGI("main", "Error starting web server!");
    return;
  }
  ESP_LOGI("main", "Web server started successfully");
}