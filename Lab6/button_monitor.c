#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#define BUTTON_GPIO 2
#define LONG_PRESS_TIME_MS 5000

static const char *TAG = "button_monitor";

void button_monitor_task(void *pvParameters)
{
    // Configure button GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    TickType_t press_start = 0;
    bool button_pressed = false;

    while(1) {
        if (gpio_get_level(BUTTON_GPIO) == 0) { // Button pressed (active low)
            if (!button_pressed) {
                press_start = xTaskGetTickCount();
                button_pressed = true;
            } else {
                // Check if button has been pressed long enough
                if ((xTaskGetTickCount() - press_start) * portTICK_PERIOD_MS >= LONG_PRESS_TIME_MS) {
                    ESP_LOGI(TAG, "Long press detected, clearing WiFi credentials");
                    
                    // Clear WiFi credentials from NVS
                    nvs_handle_t nvs_handle;
                    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
                    if (err == ESP_OK) {
                        nvs_erase_key(nvs_handle, "ssid");
                        nvs_erase_key(nvs_handle, "pass");
                        nvs_commit(nvs_handle);
                        nvs_close(nvs_handle);
                    }
                    
                    // Restart ESP32
                    esp_restart();
                }
            }
        } else {
            button_pressed = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
    }
} 