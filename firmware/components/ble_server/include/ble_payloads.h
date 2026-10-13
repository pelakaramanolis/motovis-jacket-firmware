#pragma once

#include <stdint.h>

// these four match the original byte for byte, so the android app
// decodes them without any changes

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    int32_t  latitude_e7;
    int32_t  longitude_e7;
    uint8_t  fix_valid;
} motovis_gps_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint16_t bpm;
    uint16_t raw_adc;
    uint8_t  bpm_valid;
} motovis_heart_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint16_t voltage_mv;
    uint16_t raw_adc;
    uint8_t  percentage;
    uint8_t  battery_valid;
} motovis_battery_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    int16_t  ax_mg;
    int16_t  ay_mg;
    int16_t  az_mg;
    uint8_t  alert;
} motovis_accel_payload_t;

// the new one. exactly 20 bytes so it fits a notification at the
// default mtu, no negotiation needed
typedef struct __attribute__((packed)) {
    uint32_t crash_at_ms;
    int32_t  latitude_e7;
    int32_t  longitude_e7;
    int16_t  ax_mg;
    int16_t  ay_mg;
    int16_t  az_mg;
    uint8_t  flags;
    uint8_t  sequence;
} motovis_alert_payload_t;

#define MOTOVIS_ALERT_FLAG_FIX_VALID 0x01
#define MOTOVIS_ALERT_FLAG_CANCELLED 0x02

// if any of these ever change size the app silently misreads everything,
// so fail the build instead
_Static_assert(sizeof(motovis_gps_payload_t)     == 13, "gps payload changed size");
_Static_assert(sizeof(motovis_heart_payload_t)   ==  9, "heart payload changed size");
_Static_assert(sizeof(motovis_battery_payload_t) == 10, "battery payload changed size");
_Static_assert(sizeof(motovis_accel_payload_t)   == 11, "accel payload changed size");
_Static_assert(sizeof(motovis_alert_payload_t)   == 20, "alert payload must fit the default mtu");
