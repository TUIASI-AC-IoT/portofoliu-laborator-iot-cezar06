#ifndef _WIFI_SCAN_H_
#define _WIFI_SCAN_H_

#include "esp_wifi.h"

#define DEFAULT_SCAN_LIST_SIZE 20

// Structure to hold scan results that can be shared with other modules
typedef struct {
    wifi_ap_record_t *ap_info;
    uint16_t count;
} wifi_scan_result_t;

// Function to perform WiFi scanning
wifi_scan_result_t* wifi_start_scan(void);

// New function that performs the actual scanning
uint16_t wifi_scan_ssid(wifi_ap_record_t* ap_records, uint16_t max_records);

#endif 