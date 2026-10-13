#include <stddef.h>

#include "alert_state.h"

void alert_state_init(alert_state_t *a, uint32_t window_ms)
{
    if (a == NULL) {
        return;
    }

    a->phase           = ALERT_IDLE;
    a->window_ms       = window_ms;
    a->crash_at_ms     = 0;
    a->x_mg            = 0;
    a->y_mg            = 0;
    a->z_mg            = 0;
    a->confirmed_count = 0;
    a->cancelled_count = 0;
}

bool alert_state_event(alert_state_t *a, const event_t *ev)
{
    if (a == NULL || ev == NULL) {
        return false;
    }

    if (ev->kind == EVENT_CRASH) {
        // already counting down. the first hit is the one we report
        if (a->phase != ALERT_IDLE) {
            return false;
        }

        a->phase       = ALERT_PENDING;
        a->crash_at_ms = ev->timestamp_ms;
        a->x_mg        = ev->data.crash.x_mg;
        a->y_mg        = ev->data.crash.y_mg;
        a->z_mg        = ev->data.crash.z_mg;
        return true;
    }

    // only a press counts, and only the cancel button
    if (ev->kind == EVENT_BUTTON &&
        ev->data.button.button == EVENT_BUTTON_ALERT_CANCEL &&
        ev->data.button.pressed) {

        // nothing to cancel if we aren't counting down
        if (a->phase != ALERT_PENDING) {
            return false;
        }

        a->phase = ALERT_CANCELLED;
        a->cancelled_count++;
        return true;
    }

    return false;
}

bool alert_state_tick(alert_state_t *a, uint32_t now_ms)
{
    if (a == NULL || a->phase != ALERT_PENDING) {
        return false;
    }

    // unsigned subtraction, so it still behaves when the clock wraps round
    if ((uint32_t) (now_ms - a->crash_at_ms) < a->window_ms) {
        return false;
    }

    a->phase = ALERT_CONFIRMED;
    a->confirmed_count++;
    return true;
}

void alert_state_ack(alert_state_t *a)
{
    if (a == NULL) {
        return;
    }

    if (a->phase == ALERT_CONFIRMED || a->phase == ALERT_CANCELLED) {
        a->phase = ALERT_IDLE;
    }
}

const char *alert_phase_name(alert_phase_t p)
{
    switch (p) {
    case ALERT_IDLE:      return "idle";
    case ALERT_PENDING:   return "pending";
    case ALERT_CONFIRMED: return "confirmed";
    case ALERT_CANCELLED: return "cancelled";
    }
    return "unknown";
}
