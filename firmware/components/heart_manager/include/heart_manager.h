#ifndef HEART_MANAGER_H
#define HEART_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t timestamp_ms;
    uint16_t bpm;
    uint16_t raw;          // latest adc value, useful during bring-up
    uint8_t  bpm_valid;    // 0 when no finger, or not enough beats yet
    uint32_t beat_count;   // cumulative - beats are events, so this never resets
    uint32_t sequence;
} heart_reading_t;

esp_err_t heart_manager_init(void);
bool heart_manager_get_latest(heart_reading_t *out);

#endif // HEART_MANAGER_H