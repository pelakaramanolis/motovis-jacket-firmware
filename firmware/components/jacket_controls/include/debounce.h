#ifndef DEBOUNCE_H
#define DEBOUNCE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t settle_ms;    // how long the input must hold still to count
    bool     stable;       // the state we currently believe
    bool     candidate;    // what the pin is showing right now
    uint32_t since_ms;     // when candidate last changed
} debounce_t;

void debounce_init(debounce_t *d, uint32_t settle_ms, bool initial);

// returns true when the believed state changed on this call
bool debounce_update(debounce_t *d, bool raw, uint32_t t_ms);

#endif // DEBOUNCE_H