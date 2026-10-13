#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "debounce.h"

#define SETTLE_MS 30
#define MAX_MS    600

static int failures = 0;

static void check(const char *name, bool ok)
{
    printf("  %-48s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) { failures++; }
}

static bool sig[MAX_MS];

static void fill(uint32_t from, uint32_t to, bool level)
{
    for (uint32_t t = from; t < to && t < MAX_MS; t++) {
        sig[t] = level;
    }
}

// alternates every period_ms to imitate contact bounce
static void chatter(uint32_t from, uint32_t to, uint32_t period_ms)
{
    for (uint32_t t = from; t < to && t < MAX_MS; t++) {
        sig[t] = (((t - from) / period_ms) % 2) == 0;
    }
}

static int run(bool *final_state)
{
    debounce_t d;
    debounce_init(&d, SETTLE_MS, false);

    int changes = 0;
    for (uint32_t t = 0; t < MAX_MS; t++) {
        if (debounce_update(&d, sig[t], t)) {
            changes++;
        }
    }

    *final_state = d.stable;
    return changes;
}

int main(void)
{
    bool final_state;
    int  changes;

    printf("\ndebouncer\n");

    fill(0, 100, false);
    fill(100, MAX_MS, true);
    changes = run(&final_state);
    check("clean press: one change", changes == 1);
    check("clean press: ends pressed", final_state);

    fill(0, 100, false);
    chatter(100, 130, 3);
    fill(130, MAX_MS, true);
    changes = run(&final_state);
    check("bouncy press: still one change", changes == 1);
    check("bouncy press: ends pressed", final_state);

    fill(0, 100, false);
    fill(100, 300, true);
    chatter(300, 330, 3);
    fill(330, MAX_MS, false);
    changes = run(&final_state);
    check("bouncy release: two changes total", changes == 2);
    check("bouncy release: ends released", !final_state);

    fill(0, MAX_MS, false);
    fill(100, 110, true);
    changes = run(&final_state);
    check("10 ms glitch: ignored entirely", changes == 0);
    check("10 ms glitch: still released", !final_state);

    fill(0, 100, false);
    fill(100, 300, true);
    fill(300, MAX_MS, false);
    changes = run(&final_state);
    check("press and release: two changes", changes == 2);

    chatter(0, MAX_MS, 5);
    changes = run(&final_state);
    check("constant chatter: never settles", changes == 0);

    printf("\n%s\n\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
