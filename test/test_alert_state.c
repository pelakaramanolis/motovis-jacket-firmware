#include <stdio.h>
#include <string.h>

#include "alert_state.h"

#define WINDOW_MS 5000

static int checks   = 0;
static int failures = 0;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

static event_t crash_at(uint32_t t, int16_t x, int16_t y, int16_t z)
{
    event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind              = EVENT_CRASH;
    ev.timestamp_ms      = t;
    ev.data.crash.x_mg   = x;
    ev.data.crash.y_mg   = y;
    ev.data.crash.z_mg   = z;
    return ev;
}

static event_t button_at(uint32_t t, event_button_t b, bool pressed)
{
    event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind                 = EVENT_BUTTON;
    ev.timestamp_ms         = t;
    ev.data.button.button   = b;
    ev.data.button.pressed  = pressed;
    return ev;
}

// a crash with nobody cancelling should go out
static void test_confirms_after_window(void)
{
    alert_state_t a;
    alert_state_init(&a, WINDOW_MS);

    check(a.phase == ALERT_IDLE, "starts idle");
    check(!alert_state_tick(&a, 1000), "tick while idle does nothing");

    event_t c = crash_at(10000, 5000, 0, 1000);
    check(alert_state_event(&a, &c), "crash changes phase");
    check(a.phase == ALERT_PENDING, "crash goes to pending");
    check(a.x_mg == 5000 && a.z_mg == 1000, "crash payload kept");
    check(a.crash_at_ms == 10000, "crash time kept");

    check(!alert_state_tick(&a, 12000), "still pending halfway through");
    check(!alert_state_tick(&a, 14999), "still pending 1 ms short");
    check(a.phase == ALERT_PENDING, "phase unchanged before window ends");

    check(alert_state_tick(&a, 15000), "confirms exactly on the window");
    check(a.phase == ALERT_CONFIRMED, "phase is confirmed");
    check(a.confirmed_count == 1, "one confirmed");
    check(a.cancelled_count == 0, "nothing cancelled");

    check(!alert_state_tick(&a, 20000), "no second confirm from more ticks");
    check(a.confirmed_count == 1, "still one confirmed");

    alert_state_ack(&a);
    check(a.phase == ALERT_IDLE, "ack puts it back to idle");
}

// rider presses cancel in time
static void test_cancel_inside_window(void)
{
    alert_state_t a;
    alert_state_init(&a, WINDOW_MS);

    event_t c = crash_at(10000, 5000, 0, 1000);
    alert_state_event(&a, &c);

    event_t cancel = button_at(10460, EVENT_BUTTON_ALERT_CANCEL, true);
    check(alert_state_event(&a, &cancel), "cancel changes phase");
    check(a.phase == ALERT_CANCELLED, "phase is cancelled");
    check(a.cancelled_count == 1, "one cancelled");
    check(a.confirmed_count == 0, "nothing confirmed");

    check(!alert_state_tick(&a, 99999), "window no longer matters once cancelled");
    check(a.confirmed_count == 0, "a cancelled alert never confirms");

    alert_state_ack(&a);
    check(a.phase == ALERT_IDLE, "ack after cancel goes idle");
}

// the things that should be ignored
static void test_ignores(void)
{
    alert_state_t a;
    alert_state_init(&a, WINDOW_MS);

    event_t early = button_at(500, EVENT_BUTTON_ALERT_CANCEL, true);
    check(!alert_state_event(&a, &early), "cancel with no alert does nothing");
    check(a.phase == ALERT_IDLE, "still idle");

    event_t c = crash_at(10000, 5000, 0, 1000);
    alert_state_event(&a, &c);

    event_t release = button_at(10200, EVENT_BUTTON_ALERT_CANCEL, false);
    check(!alert_state_event(&a, &release), "releasing the button is not a cancel");
    check(a.phase == ALERT_PENDING, "still pending after a release");

    event_t pairing = button_at(10300, EVENT_BUTTON_PAIRING, true);
    check(!alert_state_event(&a, &pairing), "the other button is not a cancel");
    check(a.phase == ALERT_PENDING, "still pending after pairing press");

    event_t fix;
    memset(&fix, 0, sizeof(fix));
    fix.kind = EVENT_FIX_LOST;
    check(!alert_state_event(&a, &fix), "unrelated events do nothing");
    check(a.phase == ALERT_PENDING, "still pending after an unrelated event");
}

// a second impact must not push the deadline back
static void test_second_crash_does_not_restart(void)
{
    alert_state_t a;
    alert_state_init(&a, WINDOW_MS);

    event_t first = crash_at(10000, 5000, 0, 1000);
    alert_state_event(&a, &first);

    event_t second = crash_at(13000, 7000, 0, 1000);
    check(!alert_state_event(&a, &second), "second crash changes nothing");
    check(a.crash_at_ms == 10000, "deadline still measured from the first");
    check(a.x_mg == 5000, "payload is still the first impact");

    check(alert_state_tick(&a, 15000), "confirms 5 s after the first, not the second");
    check(a.confirmed_count == 1, "one confirmed, not two");
}

// pressing cancel after it already went out is too late
static void test_cancel_after_confirm(void)
{
    alert_state_t a;
    alert_state_init(&a, WINDOW_MS);

    event_t c = crash_at(10000, 5000, 0, 1000);
    alert_state_event(&a, &c);
    alert_state_tick(&a, 15000);

    event_t late = button_at(15500, EVENT_BUTTON_ALERT_CANCEL, true);
    check(!alert_state_event(&a, &late), "late cancel does nothing");
    check(a.phase == ALERT_CONFIRMED, "stays confirmed");
    check(a.cancelled_count == 0, "nothing cancelled");
}

// the millisecond counter wraps after about 49 days
static void test_clock_wrap(void)
{
    alert_state_t a;
    alert_state_init(&a, WINDOW_MS);

    // crash 4096 ms before the counter wraps
    event_t c = crash_at(0xFFFFF000u, 5000, 0, 1000);
    alert_state_event(&a, &c);

    check(!alert_state_tick(&a, 0xFFFFF800u), "not yet, still before the wrap");
    check(!alert_state_tick(&a, 0x00000200u), "not yet, now past the wrap");
    check(alert_state_tick(&a, 0x00000500u), "confirms correctly across the wrap");
    check(a.confirmed_count == 1, "one confirmed across the wrap");
}

int main(void)
{
    test_confirms_after_window();
    test_cancel_inside_window();
    test_ignores();
    test_second_crash_does_not_restart();
    test_cancel_after_confirm();
    test_clock_wrap();

    printf("alert_state: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
