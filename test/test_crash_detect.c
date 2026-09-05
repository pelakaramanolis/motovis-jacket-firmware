#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "crash_detect.h"

#define THRESHOLD_MG    500
#define REFRACTORY_MS   2000
#define PERIOD_MS       100     /* 10 Hz, matching the original firmware */
#define MAX_SAMPLES     64

//motion traces

static int fill(accel_sample_t *out, const int16_t path[][3], int count)
{
    for (int i = 0; i < count; i++) {
        out[i].t_ms = (uint32_t) (i * PERIOD_MS);
        out[i].x_mg = path[i][0];
        out[i].y_mg = path[i][1];
        out[i].z_mg = path[i][2];
    }
    return count;
}

// minor motion, still jacket
static int trace_rest(accel_sample_t *out)
{
    for (int i = 0; i < 20; i++) {
        out[i].t_ms = (uint32_t) (i * PERIOD_MS);
        out[i].x_mg = (int16_t) ((i % 3) - 1);
        out[i].y_mg = (int16_t) ((i % 5) - 2);
        out[i].z_mg = (int16_t) (1000 + ((i % 7) - 3));
    }
    return 20;
}

// leaning driver
static int trace_lean(accel_sample_t *out)
{
    static const int16_t path[][3] = {
        {   0, 0, 1000 },
        {   0, 0, 1000 },
        {   0, 0, 1000 },
        { 707, 0,  707 },   // 45 degrees
        {1000, 0,    0 },   // 90 degrees
        {1000, 0,    0 },
        {1000, 0,    0 },
        {1000, 0,    0 },
    };
    return fill(out, path, 8);
}

// motion caused by walking, up and down movement
static int trace_walking(accel_sample_t *out)
{
    static const int16_t cycle[] = { 1000, 1100, 1150, 1100, 1000, 900, 850, 900 };

    for (int i = 0; i < 40; i++) {
        out[i].t_ms = (uint32_t) (i * PERIOD_MS);
        out[i].x_mg = (int16_t) ((i % 5) - 2);
        out[i].y_mg = (int16_t) ((i % 3) - 1);
        out[i].z_mg = cycle[i % 8];
    }
    return 40;
}

// real impact: roughly 5 g sustained for 300 ms, then still again
static int trace_impact(accel_sample_t *out)
{
    static const int16_t path[][3] = {
        {    0,    0, 1000 },
        {    0,    0, 1000 },
        {    0,    0, 1000 },
        { 5000,    0, 1000 },
        { 4800,  200,  900 },
        { 4600, -100, 1100 },
        {    0,    0, 1000 },
        {    0,    0, 1000 },
        {    0,    0, 1000 },
    };
    return fill(out, path, 9);
}

// harness 

typedef int (*trace_fn)(accel_sample_t *out);

typedef struct {
    const char *name;
    trace_fn    fn;
    int         expect_corrected;   // alerts the corrected detector must produce
} test_case_t;

static const test_case_t cases[] = {
    { "at rest",                trace_rest,    0 },
    { "quick lean, 90 in 200ms", trace_lean,   0 },
    { "walking",                trace_walking, 0 },
    { "impact, 300ms at ~5g",   trace_impact,  1 },
};

int main(void)
{
    accel_sample_t samples[MAX_SAMPLES];
    int failures = 0;

    printf("\n%-26s %8s %8s %10s %9s   %s\n",
           "trace", "samples", "naive", "corrected", "expected", "result");
    printf("%-26s %8s %8s %10s %9s   %s\n",
           "--------------------------", "-------", "-----", "---------",
           "--------", "------");

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        int n = cases[c].fn(samples);

        crash_naive_t naive;
        crash_detector_t fixed;
        crash_naive_init(&naive, THRESHOLD_MG);
        crash_detector_init(&fixed, THRESHOLD_MG, REFRACTORY_MS);

        int naive_alerts = 0;
        int fixed_alerts = 0;

        for (int i = 0; i < n; i++) {
            if (crash_naive_update(&naive, &samples[i]))    { naive_alerts++; }
            if (crash_detector_update(&fixed, &samples[i])) { fixed_alerts++; }
        }

        bool pass = (fixed_alerts == cases[c].expect_corrected);
        if (!pass) { failures++; }

        printf("%-26s %8d %8d %10d %9d   %s\n",
               cases[c].name, n, naive_alerts, fixed_alerts,
               cases[c].expect_corrected, pass ? "PASS" : "FAIL");
    }

    printf("\n");

    if (failures == 0) {
        printf("all %zu traces passed\n\n",
               sizeof(cases) / sizeof(cases[0]));
    } else {
        printf("%d trace(s) FAILED\n\n", failures);
    }

    return failures == 0 ? 0 : 1;
}