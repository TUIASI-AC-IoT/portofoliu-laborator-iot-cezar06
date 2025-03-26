#ifndef _HTTP_S_H_
#define _HTTP_S_H_

#include "esp_http_server.h"
#include "wifi-scan.h"

// Initialize the webserver with scan results
httpd_handle_t start_webserver(void);

// Stop the webserver
void stop_webserver(httpd_handle_t server);

// Set scan results to be used by the web interface
void set_scan_results(wifi_scan_result_t* results);

#endif