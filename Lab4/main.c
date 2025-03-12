/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#define CONFIG_ESP_WIFI_SSID "lab-iot"
#define CONFIG_ESP_WIFI_PASS "IoT-IoT-IoT"
#define CONFIG_ESP_MAXIMUM_RETRY 5
#define CONFIG_LOCAL_PORT 10001

#define LED_GPIO 4
#define BUTTON_GPIO 0 // Boot button
#define SERVICE_NAME "_control_led"
#define SERVICE_PROTO "_udp"
#define SERVICE_PORT CONFIG_LOCAL_PORT

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASS);
        return true;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    return false;
}

static void handle_led_command(const char *command)
{
    if (strstr(command, "GPIO4=0") != NULL)
    {
        gpio_set_level(LED_GPIO, 0);
        ESP_LOGI(TAG, "LED turned OFF");
    }
    else if (strstr(command, "GPIO4=1") != NULL)
    {
        gpio_set_level(LED_GPIO, 1);
        ESP_LOGI(TAG, "LED turned ON");
    }
}

static void init_gpio(void)
{
    gpio_config_t io_conf = {};
    // LED configuration
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LED_GPIO);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    // Button configuration
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUTTON_GPIO);
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
}

static void udp_task(void *pvParameters)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family = 0;
    int ip_protocol = 0;

    /*
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(CONFIG_PEER_IP_ADDR); // unde #define CONFIG_PEER_IP_ADDR "192.168.89.abc"
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(CONFIG_PEER_PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    */

    struct sockaddr_in local_addr;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(CONFIG_LOCAL_PORT);
    ip_protocol = IPPROTO_IP;
    addr_family = AF_INET;

    while (1)
    {
        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr));
        if (err < 0)
        {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", CONFIG_LOCAL_PORT);

        while (1)
        {

            struct sockaddr source_addr;
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, &source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0)
            {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else
            {
                inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                ESP_LOGI(TAG, "%s", rx_buffer);
                handle_led_command(rx_buffer);
            }

            vTaskDelay(200 / portTICK_PERIOD_MS);
        }

        if (sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

static void mdns_query_task(void *pvParameters)
{
    while (1)
    {
        ESP_LOGI(TAG, "Browsing for all ESP32 devices...");

        mdns_result_t *results = NULL;
        esp_err_t err = mdns_query_ptr("_esp32", "_udp", 3000, 20, &results);

        if (err)
        {
            ESP_LOGE(TAG, "mDNS query failed: %d", err);
            vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5 seconds before next query
            continue;
        }

        if (!results)
        {
            ESP_LOGI(TAG, "No ESP32 devices found!");
        }
        else
        {
            mdns_result_t *r = results;
            while (r)
            {
                ESP_LOGI(TAG, "Found ESP32 device:");
                ESP_LOGI(TAG, "  Hostname: %s", r->hostname);
                ESP_LOGI(TAG, "  IP: " IPSTR, IP2STR(&(r->addr->addr.u_addr.ip4)));
                r = r->next;
            }
            mdns_query_results_free(results);
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5 seconds before next query
    }
}

static void button_task(void *pvParameters)
{
    while (1)
    {
        if (gpio_get_level(BUTTON_GPIO) == 0)
        { // Button pressed
            ESP_LOGI(TAG, "Button pressed, searching for LED control services...");

            mdns_result_t *results = NULL;
            esp_err_t err = mdns_query_ptr(SERVICE_NAME, SERVICE_PROTO, 5000, 20, &results);
            if (err)
            {
                ESP_LOGE(TAG, "mDNS query failed: %d", err);
                vTaskDelay(pdMS_TO_TICKS(100)); // Debounce
                continue;
            }

            if (!results)
            {
                ESP_LOGI(TAG, "No LED control services found!");
                vTaskDelay(pdMS_TO_TICKS(100)); // Debounce
                continue;
            }

            // Count valid results
            int num_results = 0;
            mdns_result_t *curr = results;
            while (curr)
            {
                num_results++;
                curr = curr->next;
            }

            if (num_results > 0)
            {
                // Randomly select one result
                int selected = esp_random() % num_results;
                curr = results;
                for (int i = 0; i < selected; i++)
                {
                    curr = curr->next;
                }

                // Send UDP message to the selected service
                struct sockaddr_in dest_addr;
                dest_addr.sin_addr.s_addr = ((struct ip4_addr *)(curr->addr))->addr;
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(curr->port);

                int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
                if (sock >= 0)
                {
                    // Toggle message based on current LED state
                    static bool led_state = false;
                    led_state = !led_state;
                    const char *message = led_state ? "GPIO4=1" : "GPIO4=0";

                    sendto(sock, message, strlen(message), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                    char addr_str[16];
                    inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
                    ESP_LOGI(TAG, "Sent %s to %s:%d", message, addr_str, curr->port);
                    close(sock);
                }
            }

            mdns_query_results_free(results);
            vTaskDelay(pdMS_TO_TICKS(100)); // Debounce
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // Poll button every 50ms
    }
}

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

    // Initialize GPIO
    init_gpio();

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    bool connected = wifi_init_sta();

    if (connected)
    {
        // Initialize mDNS
        ESP_ERROR_CHECK(mdns_init());
        // Set mDNS hostname (instance name) as esp32-familyname
        ESP_ERROR_CHECK(mdns_hostname_set("esp32-tudor"));
        // Set default instance
        ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Device"));

        // Add service
        ESP_ERROR_CHECK(mdns_service_add(NULL, SERVICE_NAME, SERVICE_PROTO, SERVICE_PORT, NULL, 0));

        mdns_service_add(NULL, "_esp32", "_udp", 80, NULL, 0);
        mdns_service_instance_name_set("_esp32", "_udp", "ESP32 Device");

        // Start the tasks
        xTaskCreate(mdns_query_task, "mdns_query", 4096, NULL, 5, NULL);
        xTaskCreate(udp_task, "udp_task", 4096, NULL, 5, NULL);
        xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
    }
}