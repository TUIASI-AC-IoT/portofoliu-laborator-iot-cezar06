#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "wifi-scan.h"

static const char *TAG = "wifi-scan";

static wifi_scan_result_t scan_result = {
    .ap_info = NULL,
    .count = 0};

wifi_scan_result_t *wifi_start_scan(void)
{
    // Free previous results if any
    if (scan_result.ap_info != NULL)
    {
        free(scan_result.ap_info);
        scan_result.ap_info = NULL;
        scan_result.count = 0;
    }

    // Allocate memory for scan results
    scan_result.ap_info = malloc(sizeof(wifi_ap_record_t) * DEFAULT_SCAN_LIST_SIZE);
    if (scan_result.ap_info == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        return NULL;
    }

    ESP_LOGI(TAG, "Starting WiFi scan...");
    scan_result.count = wifi_scan_ssid(scan_result.ap_info, DEFAULT_SCAN_LIST_SIZE);

    ESP_LOGI(TAG, "Found %d access points:", scan_result.count);
    for (int i = 0; i < scan_result.count; i++)
    {
        ESP_LOGI(TAG, "SSID: %s, RSSI: %d, Channel: %d",
                 scan_result.ap_info[i].ssid,
                 scan_result.ap_info[i].rssi,
                 scan_result.ap_info[i].primary);
    }

    return &scan_result;
}

uint16_t wifi_scan_ssid(wifi_ap_record_t *ap_records, uint16_t max_records)
{
    // Initialize Wi-Fi in STA mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Add a small delay after WiFi initialization
    vTaskDelay(pdMS_TO_TICKS(100));

    // Configure scan parameters
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true};

    // Start scanning
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    // Get the scan results
    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    if (ap_count > max_records)
    {
        ap_count = max_records; // Limit to the size of the buffer
    }
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    return ap_count; // Return the number of access points found
}