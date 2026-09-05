#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t timestamp_ms;   // when this sample was taken
    uint16_t voltage_mv;     // battery voltage in millivolts
    uint8_t  percentage;     // estimated charge remaining, 0-100
    uint8_t  valid;          // 0 if the reading looks implausible
    uint32_t sequence;       // increments once per new sample
} battery_sample_t;

esp_err_t battery_manager_init(void);
bool battery_manager_get_latest_sample(battery_sample_t *out_sample);

#endif /* BATTERY_MANAGER_H */