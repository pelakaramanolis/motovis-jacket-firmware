#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "ble_payloads.h"

// which characteristic you're publishing to
typedef enum {
    BLE_CHAR_GPS = 0,
    BLE_CHAR_HEART,
    BLE_CHAR_BATTERY,
    BLE_CHAR_ACCEL,
    BLE_CHAR_ALERT,
    BLE_CHAR_COUNT,
} ble_char_t;

// brings up nvs, the controller, bluedroid, and starts advertising
esp_err_t ble_server_init(void);

// writes the payload into the attribute table, and notifies if somebody
// subscribed. the stored value is what a phone reads when it connects later
esp_err_t ble_server_publish(ble_char_t which, const void *payload, uint16_t len);

bool ble_server_connected(void);
bool ble_server_subscribed(ble_char_t which);
