#ifndef CRASH_DETECT_H
#define CRASH_DETECT_H

#include <stdbool.h>
#include <stdint.h>

// one accelerometer reading (milli-g)
typedef struct {
    uint32_t t_ms;
    int16_t  x_mg;
    int16_t  y_mg;
    int16_t  z_mg;
} accel_sample_t;

// earth's gravity
#define CRASH_GRAVITY_MG 1000

/*
 * magnitude of the difference between consecutive raw samples
 * tests can demonstrate exactly what it gets wrong
 */
typedef struct {
    int32_t threshold_mg;
    bool    have_prev;
    int16_t prev_x_mg;
    int16_t prev_y_mg;
    int16_t prev_z_mg;
} crash_naive_t;

void crash_naive_init(crash_naive_t *d, int32_t threshold_mg);
bool crash_naive_update(crash_naive_t *d, const accel_sample_t *s);

/*
 * tests how far total acceleration magnitude
 * departs from 1 g, and refuses to re-fire during a refractory window
 */
typedef struct {
    int32_t  threshold_mg;
    uint32_t refractory_ms;
    bool     alerted;
    uint32_t last_alert_ms;
} crash_detector_t;

void crash_detector_init(crash_detector_t *d, int32_t threshold_mg,
                         uint32_t refractory_ms);
bool crash_detector_update(crash_detector_t *d, const accel_sample_t *s);

#endif /* CRASH_DETECT_H */