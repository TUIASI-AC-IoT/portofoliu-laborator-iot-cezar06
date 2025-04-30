/***************************************************************************//**
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
#include "em_common.h"
#include "app_assert.h"
#include "sl_bluetooth.h"
#include "gatt_db.h"
#include "app_log.h" // Include app_log for logging messages
#include "app.h"

#include "em_cmu.h"
#include "em_gpio.h"

// Use the GATT database handles
#define LED_CHAR_HANDLE    gattdb_LED_IO     // Assuming gattdb_LED_IO is the handle for LED characteristic from gatt_db.h
#define BUTTON_CHAR_HANDLE gattdb_BUTTON_IO  // Assuming gattdb_BUTTON_IO is the handle for BUTTON characteristic from gatt_db.h

#define GPIO_PORT_LED     gpioPortA
#define GPIO_PIN_LED      4
#define GPIO_PORT_BUTTON  gpioPortC
#define GPIO_PIN_BUTTON   7
#define BUTTON_IRQn       GPIO_ODD_IRQn // Assuming PC7 uses ODD IRQ Handler

static bool button_io_notification_enabled = false;
// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;
// Connection handle
static uint8_t connection_handle = 0xff;

void GPIO_ODD_IRQHandler(void)
{
  // Stergere flag intrerupere
  uint32_t interruptMask = GPIO_IntGet();
  GPIO_IntClear(interruptMask);

  // Check if the interrupt is from the correct pin
  if (interruptMask & (1 << GPIO_PIN_BUTTON)) {
      uint8_t button_state = GPIO_PinInGet(GPIO_PORT_BUTTON, GPIO_PIN_BUTTON) ? 1 : 0;
      sl_status_t sc;

      // Update GATT attribute value
      sc = sl_bt_gatt_server_write_attribute_value(BUTTON_CHAR_HANDLE, 0, sizeof(button_state), &button_state);
      app_log_status_error(sc);

      // Notify client if enabled and connected
      if (button_io_notification_enabled && connection_handle != 0xff) {
          sc = sl_bt_gatt_server_notify_all(BUTTON_CHAR_HANDLE, sizeof(button_state), &button_state);
          // Optional: Log error if notification fails, but don't assert
          if(sc != SL_STATUS_OK){
              app_log_warning("Failed to send notification, sc=0x%lx\n", sc);
          }
      }
  }
}

/**************************************************************************//**
 * Application Init.
 *****************************************************************************/
SL_WEAK void app_init(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application init code here!                         //
  // This is called once during start-up.                                    //
  /////////////////////////////////////////////////////////////////////////////

  // Activare ramura clock periferic GPIO
  CMU_ClockEnable(cmuClock_GPIO, true);
  // Configurare GPIOA 04 ca iesire (LED)
  GPIO_PinModeSet(GPIO_PORT_LED, GPIO_PIN_LED, gpioModePushPull, 1); // Start with LED OFF assuming '1' is OFF based on schematic or preference
  // Configurare GPIOC 07 ca intrare (buton) cu pull-up si filtru
  GPIO_PinModeSet(GPIO_PORT_BUTTON, GPIO_PIN_BUTTON, gpioModeInputPullFilter, 1);
  // Configurare intrerupere pentru buton pe ambele fronturi
  GPIO_ExtIntConfig(GPIO_PORT_BUTTON, GPIO_PIN_BUTTON, GPIO_PIN_BUTTON, true, true, true); // Use pin number also as IRQ number for simplicity if mapping allows
  // Activare intrerupere
  NVIC_ClearPendingIRQ(BUTTON_IRQn);
  NVIC_EnableIRQ(BUTTON_IRQn);

  // Initial read of button state and update GATT DB
  uint8_t initial_button_state = GPIO_PinInGet(GPIO_PORT_BUTTON, GPIO_PIN_BUTTON) ? 1 : 0;
  sl_status_t sc = sl_bt_gatt_server_write_attribute_value(BUTTON_CHAR_HANDLE, 0, sizeof(initial_button_state), &initial_button_state);
  app_assert_status(sc); // Should succeed on init
}

/**************************************************************************//**
 * Application Process Action.
 *****************************************************************************/
SL_WEAK void app_process_action(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application code here!                              //
  // This is called infinitely.                                              //
  // Do not call blocking functions from here!                               //
  /////////////////////////////////////////////////////////////////////////////
}

/**************************************************************************//**
 * Bluetooth stack event handler.
 * This overrides the dummy weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:
      app_log("System Boot\r\n");
      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);

      // Generate data for advertising
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                 sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      // Set advertising interval to 100ms.
      sc = sl_bt_advertiser_set_timing(
        advertising_set_handle,
        160, // min. adv. interval (milliseconds * 1.6 = 100ms)
        160, // max. adv. interval (milliseconds * 1.6 = 100ms)
        0,   // adv. duration
        0);  // max. num. adv. events
      app_assert_status(sc);
      // Start advertising and enable connections.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_advertiser_connectable_scannable);
      app_assert_status(sc);

      // Configure security manager: enable bonding, require MITM for 'authenticated' pairing (if used)
      // Using 0x01 flag: Bondable mode enabled. No MITM required (Just Works pairing often sufficient for 'bonded').
      // Change flags if MITM (passkey/numeric comparison) is desired for authenticated characteristics.
      sc = sl_bt_sm_configure(0x01, sl_bt_sm_io_capability_noinputnooutput); // Flags: 0x01 = Bonding enabled
      app_assert_status(sc);

      // Allow bonding requests
      sc = sl_bt_sm_set_bondable_mode(1); // 1 = bondable
      app_assert_status(sc);
      app_log("Bondable mode set\r\n");

      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      connection_handle = evt->data.evt_connection_opened.connection;
      app_log("Connection opened, handle: %d\r\n", connection_handle);

      // Optionally increase security (request bonding/encryption) if needed right away.
      // For characteristics marked as 'Bonded', the stack usually handles this implicitly
      // when the client tries to access them. However, explicit call can enforce it earlier.
      // sc = sl_bt_sm_increase_security(connection_handle);
      // if (sc != SL_STATUS_OK && sc != SL_STATUS_INVALID_STATE){ // Ignore SL_STATUS_INVALID_STATE if already secure
      //   app_log_warning("sl_bt_sm_increase_security failed: 0x%lx\r\n", sc);
      // }
      break;

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      app_log("Connection closed, reason: 0x%x, handle: %d\r\n",
              evt->data.evt_connection_closed.reason,
              evt->data.evt_connection_closed.connection);
      connection_handle = 0xff; // Reset connection handle
      button_io_notification_enabled = false; // Reset notification status on disconnect

      // Generate data for advertising
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                 sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      // Restart advertising after client has disconnected.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_advertiser_connectable_scannable);
      app_assert_status(sc);
      break;

    // -------------------------------
    // This event indicates that the bonding process completed successfully.
    case sl_bt_evt_sm_bonded_id:
      app_log("Bonding successful, handle: %d\r\n", evt->data.evt_sm_bonded.connection);
      break;

    // -------------------------------
    // This event indicates that the bonding process failed.
    case sl_bt_evt_sm_bonding_failed_id:
      app_log("Bonding failed, reason: 0x%x, handle: %d\r\n",
              evt->data.evt_sm_bonding_failed.reason,
              evt->data.evt_sm_bonding_failed.connection);
      // Optionally close the connection if bonding is mandatory for operation
      // sc = sl_bt_connection_close(evt->data.evt_sm_bonding_failed.connection);
      // app_assert_status(sc);
      break;

    // -------------------------------
    // This event occurs when the remote device writes characteristics.
    case sl_bt_evt_gatt_server_attribute_value_id:
      // Check if the write operation is on the LED characteristic
      if (LED_CHAR_HANDLE == evt->data.evt_gatt_server_attribute_value.attribute) {
        uint8_t recv_val;
        size_t recv_len;
        // Read the written value - Although we get the value in the event data, reading ensures we have the correct current value
        // Note: evt->data.evt_gatt_server_attribute_value.value contains the data too.
        sc = sl_bt_gatt_server_read_attribute_value(LED_CHAR_HANDLE,
                                                    0,
                                                    sizeof(recv_val),
                                                    &recv_len,
                                                    &recv_val);
        app_assert_status(sc);

        if (recv_len > 0) {
          if (recv_val == 0) {
            GPIO_PinOutClear(GPIO_PORT_LED, GPIO_PIN_LED);
            app_log("LED OFF\r\n");
          } else {
            GPIO_PinOutSet(GPIO_PORT_LED, GPIO_PIN_LED);
             app_log("LED ON\r\n");
          }
          // Optionally log the raw value: app_log("LED = %d\r\n", recv_val);
        }
      }
      break;

    // -------------------------------
    // This event occurs when the characteristic status flags for a client change.
    case sl_bt_evt_gatt_server_characteristic_status_id:
      // Check if the status change is for the BUTTON characteristic
      if (BUTTON_CHAR_HANDLE == evt->data.evt_gatt_server_characteristic_status.characteristic) {
        // Check if the client enabled notifications (GATT Client Characteristic Configuration descriptor flags)
        if (sl_bt_gatt_notification == (sl_bt_gatt_server_characteristic_status_flag_t)evt->data.evt_gatt_server_characteristic_status.status_flags) {
           if (evt->data.evt_gatt_server_characteristic_status.client_config_flags & sl_bt_gatt_notification) {
              app_log("Notificare activata pentru caracteristica BUTTON\r\n");
              button_io_notification_enabled = true;
              // Optionally send current state upon enabling notification
               uint8_t button_state = GPIO_PinInGet(GPIO_PORT_BUTTON, GPIO_PIN_BUTTON) ? 1 : 0;
               sc = sl_bt_gatt_server_notify_all(BUTTON_CHAR_HANDLE, sizeof(button_state), &button_state);
                if(sc != SL_STATUS_OK){
                  app_log_warning("Failed to send initial notification, sc=0x%lx\n", sc);
                }
            } else {
              app_log("Notificare dezactivata pentru caracteristica BUTTON\r\n");
              button_io_notification_enabled = false;
            }
        }
      }
      break;

    // -------------------------------
    // Default event handler.
    default:
      break;
  }
}
