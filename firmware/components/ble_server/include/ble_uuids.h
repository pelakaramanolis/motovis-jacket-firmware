#pragma once

#include <stdint.h>

#include "esp_bt_defs.h"

// one custom service, and a characteristic per sensor stream.
// these are the same uuids the original used, so the android app
// doesn't need changing
extern const uint8_t MOTOVIS_SERVICE_UUID[16];
extern const uint8_t MOTOVIS_CHAR_UUID_HEART_RATE[16];
extern const uint8_t MOTOVIS_CHAR_UUID_BATTERY[16];
extern const uint8_t MOTOVIS_CHAR_UUID_ACCEL[16];
extern const uint8_t MOTOVIS_CHAR_UUID_GYRO[16];
extern const uint8_t MOTOVIS_CHAR_UUID_GPS[16];

// new one. carries a confirmed alert, not a flag that might get missed
extern const uint8_t MOTOVIS_CHAR_UUID_ALERT[16];

void motovis_uuid128_to_esp(const uint8_t source[16], esp_bt_uuid_t *dest);
