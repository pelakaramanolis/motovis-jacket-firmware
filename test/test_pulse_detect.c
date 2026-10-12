#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pulse_detect.h"

#define SAMPLE_MS    10      // 100 hz
#define BASELINE     1800
#define AMPLITUDE    400
#define REFRACTORY   250
#define WINDOW_MS    2000

static int failures = 0;

static void check(const char *name, bool ok)
{
    printf("  %-50s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) { failures++; }
}

// one heartbeat: the pressure wave, then the dicrotic notch when the aortic
// valve shuts. notch is its height as a fraction of the main peak
static double beat_shape(double phase, double notch)
{
    if (phase < 0.05) { return 0.0; }
    if (phase < 0.15) { return (phase - 0.05) / 0.10; }
    if (phase < 0.35) { return 1.0 - (phase - 0.15) / 0.20; }
    if (phase < 0.45) { return notch * ((phase - 0.35) / 0.10); }
    if (phase < 0.60) { return notch * (1.0 - (phase - 0.45) / 0.15); }
    return 0.0;
}

static uint32_t rng_state = 12345;

static int noise(int spread)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (int) ((rng_state >> 16) % (uint32_t) (2 * spread + 1)) - spread;
}

static int run_pulse(uint32_t bpm_in, uint32_t seconds, uint32_t refractory_ms,
                     double notch, uint16_t *bpm_out, bool *bpm_valid_out)
{
    pulse_detector_t d;
    pulse_detector_init(&d, refractory_ms, WINDOW_MS);

    uint32_t period_ms = 60000U / bpm_in;
    uint32_t samples   = (seconds * 1000U) / SAMPLE_MS;
    int      beats     = 0;

    *bpm_out       = 0;
    *bpm_valid_out = false;

    for (uint32_t i = 0; i < samples; i++) {
        uint32_t t     = i * SAMPLE_MS;
        double   phase = (double) (t % period_ms) / (double) period_ms;
        uint16_t s     = (uint16_t) (BASELINE + beat_shape(phase, notch) * AMPLITUDE);

        pulse_result_t r = pulse_detector_update(&d, s, t);
        if (r.beat) { beats++; }

        *bpm_out       = r.bpm;
        *bpm_valid_out = r.bpm_valid;
    }

    return beats;
}

static int run_flat(int noise_spread, uint16_t *bpm_out, bool *bpm_valid_out)
{
    pulse_detector_t d;
    pulse_detector_init(&d, REFRACTORY, WINDOW_MS);

    int beats = 0;
    *bpm_out       = 0;
    *bpm_valid_out = false;

    for (uint32_t i = 0; i < 1000; i++) {
        uint32_t t = i * SAMPLE_MS;
        uint16_t s = (uint16_t) (2000 + noise(noise_spread));

        pulse_result_t r = pulse_detector_update(&d, s, t);
        if (r.beat) { beats++; }

        *bpm_out       = r.bpm;
        *bpm_valid_out = r.bpm_valid;
    }

    return beats;
}

int main(void)
{
    uint16_t bpm;
    bool     valid;
    int      beats;

    printf("\npulse detector\n");

    beats = run_pulse(60, 10, REFRACTORY, 0.40, &bpm, &valid);
    check("60 bpm, small notch: ~10 beats in 10 s", beats >= 9 && beats <= 10);
    check("60 bpm, small notch: reports a valid bpm", valid);
    check("60 bpm, small notch: within 5 of 60", bpm >= 55 && bpm <= 65);

    beats = run_pulse(120, 10, REFRACTORY, 0.40, &bpm, &valid);
    check("120 bpm, small notch: ~20 beats in 10 s", beats >= 19 && beats <= 20);
    check("120 bpm, small notch: within 6 of 120", bpm >= 114 && bpm <= 126);

    // a notch at 65% clears the halfway threshold, so the threshold alone
    // can't reject it. it lands ~330 ms after the real beat
    beats = run_pulse(60, 10, 250, 0.65, &bpm, &valid);
    check("big notch, 250 ms refractory: double-counts", beats >= 18);
    check("big notch, 250 ms refractory: bpm roughly double", bpm >= 100);

    beats = run_pulse(60, 10, 400, 0.65, &bpm, &valid);
    check("big notch, 400 ms refractory: ~10 beats", beats >= 9 && beats <= 10);
    check("big notch, 400 ms refractory: within 5 of 60", bpm >= 55 && bpm <= 65);

    beats = run_flat(0, &bpm, &valid);
    check("flat signal: no beats", beats == 0);
    check("flat signal: bpm not valid", !valid);

    beats = run_flat(10, &bpm, &valid);
    check("noise but no pulse: no beats", beats == 0);
    check("noise but no pulse: bpm not valid", !valid);

    printf("\n%s\n\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
