#ifndef _WIFI_SCAN_H_
#define _WIFI_SCAN_H_

#include "esp_wifi.h"

#define DEFAULT_SCAN_LIST_SIZE 20


typedef struct {
    wifi_ap_record_t *ap_info;
    uint16_t count;
} wifi_scan_result_t;

wifi_scan_result_t* wifi_start_scan(void);

#endif 