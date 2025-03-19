#ifndef _HTTP_S_H_
#define _HTTP_S_H_

#include "esp_http_server.h"
#include "wifi-scan.h"

httpd_handle_t start_webserver(void);

void stop_webserver(httpd_handle_t server);

void set_scan_results(wifi_scan_result_t* results);

#endif