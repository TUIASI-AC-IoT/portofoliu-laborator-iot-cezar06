#ifndef BUTTON_MONITOR_H
#define BUTTON_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

// GPIO pin number for the button
#define BUTTON_GPIO 2

// Time in milliseconds for long press detection
#define LONG_PRESS_TIME_MS 5000

/**
 * @brief Task that monitors button state for long presses
 * 
 * This task monitors a button connected to BUTTON_GPIO.
 * When the button is pressed for LONG_PRESS_TIME_MS milliseconds:
 * - Clears WiFi credentials from NVS
 * - Triggers a device restart
 * 
 * @param pvParameters FreeRTOS task parameters (unused)
 */
void button_monitor_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_MONITOR_H */
