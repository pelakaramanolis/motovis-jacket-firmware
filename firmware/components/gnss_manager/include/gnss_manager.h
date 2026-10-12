#ifndef GNSS_MANAGER_H
#define GNSS_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t timestamp_ms;
    int32_t  lat_e7;
    int32_t  lon_e7;
    uint8_t  satellites;
    uint8_t  fix_valid;
    uint32_t sequence;
} gnss_reading_t;

esp_err_t gnss_manager_init(void);
bool gnss_manager_get_latest(gnss_reading_t *out);

#endif // GNSS_MANAGER_H