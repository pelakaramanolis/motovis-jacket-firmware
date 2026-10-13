#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "event_bus.h"

typedef enum {
    ALERT_IDLE = 0,
    ALERT_PENDING,      // crash seen, rider has a few seconds to cancel
    ALERT_CONFIRMED,    // nobody cancelled, this one goes out
    ALERT_CANCELLED,    // rider said they're fine
} alert_phase_t;

typedef struct {
    alert_phase_t phase;
    uint32_t      window_ms;      // how long the rider gets
    uint32_t      crash_at_ms;    // when the crash that started this happened
    int16_t       x_mg, y_mg, z_mg;

    uint32_t      confirmed_count;
    uint32_t      cancelled_count;
} alert_state_t;

void alert_state_init(alert_state_t *a, uint32_t window_ms);

// feed it one event. true means the phase changed
bool alert_state_event(alert_state_t *a, const event_t *ev);

// call this regularly, it's what notices the window running out
bool alert_state_tick(alert_state_t *a, uint32_t now_ms);

// back to idle once you've dealt with a confirmed or cancelled alert
void alert_state_ack(alert_state_t *a);

const char *alert_phase_name(alert_phase_t p);
