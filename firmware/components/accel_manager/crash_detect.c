#include "crash_detect.h"

static int64_t magnitude_squared(int32_t x, int32_t y, int32_t z)
{
    return (int64_t) x * x + (int64_t) y * y + (int64_t) z * z;
}

//naive detector

void crash_naive_init(crash_naive_t *d, int32_t threshold_mg)
{
    d->threshold_mg = threshold_mg;
    d->have_prev    = false;
    d->prev_x_mg    = 0;
    d->prev_y_mg    = 0;
    d->prev_z_mg    = 0;
}

bool crash_naive_update(crash_naive_t *d, const accel_sample_t *s)
{
    bool alert = false;

    if (d->have_prev) {
        int32_t dx = (int32_t) s->x_mg - d->prev_x_mg;
        int32_t dy = (int32_t) s->y_mg - d->prev_y_mg;
        int32_t dz = (int32_t) s->z_mg - d->prev_z_mg;

        int64_t thr = (int64_t) d->threshold_mg * d->threshold_mg;
        alert = magnitude_squared(dx, dy, dz) > thr;
    } else {
        d->have_prev = true;
    }

    d->prev_x_mg = s->x_mg;
    d->prev_y_mg = s->y_mg;
    d->prev_z_mg = s->z_mg;

    return alert;
}

// corrected detector

void crash_detector_init(crash_detector_t *d, int32_t threshold_mg,
                         uint32_t refractory_ms)
{
    d->threshold_mg  = threshold_mg;
    d->refractory_ms = refractory_ms;
    d->alerted       = false;
    d->last_alert_ms = 0;
}

bool crash_detector_update(crash_detector_t *d, const accel_sample_t *s)
{
    int64_t mag_sq = magnitude_squared(s->x_mg, s->y_mg, s->z_mg);

    int64_t high = CRASH_GRAVITY_MG + d->threshold_mg;
    int64_t low  = CRASH_GRAVITY_MG - d->threshold_mg;
    if (low < 0) {
        low = 0;
    }

    bool exceeded = (mag_sq > high * high) || (mag_sq < low * low);

    if (!exceeded) {
        return false;
    }

    if (d->alerted &&
        (uint32_t) (s->t_ms - d->last_alert_ms) < d->refractory_ms) {
        return false;
    }

    d->alerted       = true;
    d->last_alert_ms = s->t_ms;
    return true;
}