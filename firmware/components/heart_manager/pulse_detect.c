#include "pulse_detect.h"

#define PULSE_INTERVAL_SLOTS 4

void pulse_detector_init(pulse_detector_t *d, uint32_t refractory_ms,
                         uint32_t window_ms)
{
    d->refractory_ms   = refractory_ms;
    d->window_ms       = window_ms;
    d->sig_min         = 0;
    d->sig_max         = 0;
    d->window_start_ms = 0;
    d->primed          = false;
    d->above           = false;
    d->have_last_beat  = false;
    d->last_beat_ms    = 0;
    d->n_intervals     = 0;
    d->next_interval   = 0;

    for (uint8_t i = 0; i < PULSE_INTERVAL_SLOTS; i++) {
        d->intervals[i] = 0;
    }
}

static void forget_intervals(pulse_detector_t *d)
{
    d->n_intervals   = 0;
    d->next_interval = 0;
}

static uint16_t average_bpm(const pulse_detector_t *d)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < d->n_intervals; i++) {
        sum += d->intervals[i];
    }

    uint32_t mean = sum / d->n_intervals;
    if (mean == 0) {
        return 0;
    }

    return (uint16_t) (60000UL / mean);
}

pulse_result_t pulse_detector_update(pulse_detector_t *d, uint16_t sample,
                                     uint32_t t_ms)
{
    pulse_result_t r = { false, 0, false };

    if (!d->primed) {
        d->sig_min         = sample;
        d->sig_max         = sample;
        d->window_start_ms = t_ms;
        d->primed          = true;
        return r;
    }

    if (sample < d->sig_min) { d->sig_min = sample; }
    if (sample > d->sig_max) { d->sig_max = sample; }

    uint16_t amplitude = (uint16_t) (d->sig_max - d->sig_min);

    if (amplitude < PULSE_MIN_AMPLITUDE) {
        d->above          = false;
        d->have_last_beat = false;
        forget_intervals(d);
        return r;
    }

    uint16_t threshold = (uint16_t) (d->sig_min + amplitude / 2);
    uint16_t rearm     = (uint16_t) (d->sig_min + amplitude / 4);

    if (!d->above && sample > threshold) {
        d->above = true;

        bool allowed = !d->have_last_beat ||
                       (uint32_t) (t_ms - d->last_beat_ms) >= d->refractory_ms;

        if (allowed) {
            if (d->have_last_beat) {
                uint32_t gap     = t_ms - d->last_beat_ms;
                uint32_t min_gap = 60000UL / PULSE_MAX_BPM;
                uint32_t max_gap = 60000UL / PULSE_MIN_BPM;

                if (gap >= min_gap && gap <= max_gap) {
                    d->intervals[d->next_interval] = (uint16_t) gap;
                    d->next_interval = (uint8_t) ((d->next_interval + 1) %
                                                  PULSE_INTERVAL_SLOTS);
                    if (d->n_intervals < PULSE_INTERVAL_SLOTS) {
                        d->n_intervals++;
                    }
                } else {
                    forget_intervals(d);
                }
            }

            d->last_beat_ms   = t_ms;
            d->have_last_beat = true;
            r.beat            = true;
        }
    } else if (d->above && sample < rearm) {
        d->above = false;
    }

    // shrink the envelope slowly so an old spike doesn't pin the threshold
    if ((uint32_t) (t_ms - d->window_start_ms) >= d->window_ms) {
        d->window_start_ms = t_ms;
        uint16_t mid = (uint16_t) (d->sig_min + amplitude / 2);
        d->sig_max = (uint16_t) (d->sig_max - (d->sig_max - mid) / 4);
        d->sig_min = (uint16_t) (d->sig_min + (mid - d->sig_min) / 4);
    }

    if (d->n_intervals >= 2) {
        r.bpm       = average_bpm(d);
        r.bpm_valid = (r.bpm >= PULSE_MIN_BPM && r.bpm <= PULSE_MAX_BPM);
    }

    return r;
}
