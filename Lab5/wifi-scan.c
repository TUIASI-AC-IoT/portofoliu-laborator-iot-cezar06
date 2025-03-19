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
    .count = 0
};

wifi_scan_result_t* wifi_start_scan(void)
{
    if (scan_result.ap_info != NULL) {
        free(scan_result.ap_info);
        scan_result.ap_info = NULL;
        scan_result.count = 0;
    }

    scan_result.ap_info = malloc(sizeof(wifi_ap_record_t) * DEFAULT_SCAN_LIST_SIZE);
    if (scan_result.ap_info == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        return NULL;
    }

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_mode_t current_mode;
    ESP_ERROR_CHECK(esp_wifi_get_mode(&current_mode));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    uint16_t ap_count = 0;
    memset(scan_result.ap_info, 0, sizeof(wifi_ap_record_t) * DEFAULT_SCAN_LIST_SIZE);

    ESP_LOGI(TAG, "Starting WiFi scan...");
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, scan_result.ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    
    scan_result.count = (ap_count < number) ? ap_count : number;
    ESP_LOGI(TAG, "Found %d access points:", scan_result.count);

    for (int i = 0; i < scan_result.count; i++) {
        ESP_LOGI(TAG, "SSID: %s, RSSI: %d, Channel: %d",
                 scan_result.ap_info[i].ssid,
                 scan_result.ap_info[i].rssi,
                 scan_result.ap_info[i].primary);
    }

    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(current_mode));
    ESP_ERROR_CHECK(esp_wifi_start());

    return &scan_result;
} 