#ifndef JACKET_CONTROLS_H
#define JACKET_CONTROLS_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    JACKET_BUTTON_PAIRING = 0,
    JACKET_BUTTON_ALERT_CANCEL,
    JACKET_BUTTON_COUNT,
} jacket_button_t;

typedef struct {
    jacket_button_t button;
    bool            pressed;      // true on press, false on release
    uint32_t        timestamp_ms;
    uint32_t        sequence;
} jacket_button_event_t;

esp_err_t jacket_controls_init(void);

// waits up to wait_ms for the next event. false means nothing arrived
bool jacket_controls_get_event(jacket_button_event_t *out, uint32_t wait_ms);

// events thrown away because the queue was full
uint32_t jacket_controls_dropped(void);

#endif // JACKET_CONTROLS_H
