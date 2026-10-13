#include "debounce.h"

void debounce_init(debounce_t *d, uint32_t settle_ms, bool initial)
{
    d->settle_ms = settle_ms;
    d->stable    = initial;
    d->candidate = initial;
    d->since_ms  = 0;
}

bool debounce_update(debounce_t *d, bool raw, uint32_t t_ms)
{
    // the pin moved - start the clock again on this new reading
    if (raw != d->candidate) {
        d->candidate = raw;
        d->since_ms  = t_ms;
        return false;
    }

    // same as what we already believe, nothing to do
    if (d->candidate == d->stable) {
        return false;
    }

    // held steady long enough to be real
    if ((uint32_t) (t_ms - d->since_ms) >= d->settle_ms) {
        d->stable = d->candidate;
        return true;
    }

    return false;
}