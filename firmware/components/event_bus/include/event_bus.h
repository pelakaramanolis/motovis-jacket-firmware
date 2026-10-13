#pragma once

#include <stdbool.h>
#include <stdint.h>

// how many events can wait before we start losing them
#define EVENT_BUS_DEPTH 16

// everything that can happen on the jacket
typedef enum {
    EVENT_CRASH,
    EVENT_BUTTON,
    EVENT_FIX_GAINED,
    EVENT_FIX_LOST,
    EVENT_BATTERY_LOW,
} event_kind_t;

// which button
typedef enum {
    EVENT_BUTTON_PAIRING,
    EVENT_BUTTON_ALERT_CANCEL,
} event_button_t;

typedef struct {
    event_kind_t kind;
    uint32_t     timestamp_ms;  // bus fills this
    uint32_t     sequence;      // bus fills this too
    union {
        struct { int16_t x_mg, y_mg, z_mg; } crash;
        struct { event_button_t button; bool pressed; } button;
        struct { uint8_t percentage; } battery;
    } data;
} event_t;

bool event_bus_init(void);

// fill in kind and the matching payload, the bus does the rest.
// never blocks. returns false if the queue was full
bool event_bus_publish(event_t *ev);

// wait_ms of 0 means look and leave straight away
bool event_bus_take(event_t *out, uint32_t wait_ms);

uint32_t    event_bus_dropped(void);
const char *event_kind_name(event_kind_t kind);
