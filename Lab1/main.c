#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#define GPIO_OUTPUT_IO 4
#define GPIO_INPUT_IO 2
#define GPIO_OUTPUT_PIN_SEL (1ULL << GPIO_OUTPUT_IO)
#define GPIO_INPUT_PIN_SEL (1ULL << GPIO_INPUT_IO)
#define ESP_INTR_FLAG_DEFAULT 0
#define LONG_PRESS_DURATION 1000 // 1 second for long press
#define NUM_BLINK_PATTERNS 3

static QueueHandle_t gpio_evt_queue = NULL;
static int button_presses = 0;
static bool led_sequence_enabled = false;
static uint32_t press_start_time = 0;
static uint32_t last_press_time = 0;
static int current_pattern = 0;

// Blinking patterns (on_time, off_time) in ms
static const struct
{
    int on_time;
    int off_time;
} blink_patterns[NUM_BLINK_PATTERNS] = {
    {500, 500}, // Pattern 0: Regular blinking
    {200, 800}, // Pattern 1: Quick blink, long pause
    {1000, 200} // Pattern 2: Long on, quick off
};

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void button_task(void *arg)
{
    uint32_t io_num;
    bool button_pressed = false;
    uint32_t current_time;
    while (1)
    {
        current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        int level = gpio_get_level(GPIO_INPUT_IO);

        if ((current_time - last_press_time) >= 200) // Debounce
        {
            if (level == 1 && !button_pressed) // Button pressed
            {
                button_pressed = true;
                press_start_time = current_time;
            }
            else if (level == 0 && button_pressed) // Button released
            {
                button_pressed = false;
                uint32_t press_duration = current_time - press_start_time;

                if (press_duration >= LONG_PRESS_DURATION)
                {
                    // Long press: cycle through patterns
                    if (led_sequence_enabled)
                    {
                        current_pattern = (current_pattern + 1) % NUM_BLINK_PATTERNS;
                        printf("Long press detected! New pattern: %d\n", current_pattern);
                    }
                }
                else if (press_duration >= 50) // Minimum press duration to avoid noise
                {
                    // Short press: toggle LED sequence
                    button_presses++;
                    led_sequence_enabled = !led_sequence_enabled;
                    printf("Short press! Count: %d, LED Sequence: %s\n",
                           button_presses, led_sequence_enabled ? "ON" : "OFF");
                }
                last_press_time = current_time;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Poll every 10ms
    }
}

static void led_task(void *arg)
{
    bool was_enabled = false;
    while (1)
    {
        if (led_sequence_enabled)
        {
            was_enabled = true;
            gpio_set_level(GPIO_OUTPUT_IO, 1);
            vTaskDelay(blink_patterns[current_pattern].on_time / portTICK_PERIOD_MS);

            gpio_set_level(GPIO_OUTPUT_IO, 0);
            vTaskDelay(blink_patterns[current_pattern].off_time / portTICK_PERIOD_MS);
        }
        else
        {
            if (was_enabled)
            {
                gpio_set_level(GPIO_OUTPUT_IO, 0);
                was_enabled = false;
            }
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
}

void app_main()
{
    // Configure output pin
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    // Configure input pin
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    // Create queue and install GPIO ISR service
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPIO_INPUT_IO, gpio_isr_handler, (void *)GPIO_INPUT_IO);

    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
    xTaskCreate(led_task, "led_task", 2048, NULL, 10, NULL);
}