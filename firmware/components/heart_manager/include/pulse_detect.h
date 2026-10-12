#ifndef PULSE_DETECT_H
#define PULSE_DETECT_H

#include <stdbool.h>
#include <stdint.h>

// below this much swing between peak and trough there's no finger on the sensor
#define PULSE_MIN_AMPLITUDE 50

// plausible human range, used to throw out nonsense intervals
#define PULSE_MIN_BPM 30
#define PULSE_MAX_BPM 240

typedef struct {
    bool     beat;        // a beat landed on this sample
    uint16_t bpm;
    bool     bpm_valid;   // false until we have enough intervals to average
} pulse_result_t;

typedef struct {
    uint32_t refractory_ms;
    uint32_t window_ms;      // how often the peak/trough envelope resets

    uint16_t sig_min;
    uint16_t sig_max;
    uint32_t window_start_ms;
    bool     primed;

    bool     above;          // currently above the threshold
    bool     have_last_beat;
    uint32_t last_beat_ms;

    uint16_t intervals[4];   // last few beat-to-beat gaps, averaged for bpm
    uint8_t  n_intervals;
    uint8_t  next_interval;
} pulse_detector_t;

void pulse_detector_init(pulse_detector_t *d, uint32_t refractory_ms,
                         uint32_t window_ms);

pulse_result_t pulse_detector_update(pulse_detector_t *d, uint16_t sample,
                                     uint32_t t_ms);

#endif // PULSE_DETECT_H