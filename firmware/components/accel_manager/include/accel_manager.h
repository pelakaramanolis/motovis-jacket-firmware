#ifndef ACCEL_MANAGER_H
#define ACCEL_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t timestamp_ms;
    int16_t  x_mg;
    int16_t  y_mg;
    int16_t  z_mg;

    uint8_t  alert;

    // how many crashes we've detected since boot
    uint32_t alert_count;

    uint32_t sequence;
} accel_reading_t;

esp_err_t accel_manager_init(void);
bool accel_manager_get_latest(accel_reading_t *out);

#endif // ACCEL_MANAGER_H