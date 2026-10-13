#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "alert_state.h"
#include "ble_server.h"
#include "event_bus.h"

#include "accel_manager.h"
#include "battery_manager.h"
#include "gnss_manager.h"
#include "heart_manager.h"
#include "jacket_controls.h"

#define STATUS_LED_GPIO 2

// how long the rider gets to cancel before it goes out
#define ALERT_WINDOW_MS 5000

// counts how many crash alerts the old transient flag loses, next to
// the same crashes arriving safely on the bus. instrumentation, on
// purpose - set to 0 and the flag is simply ignored
#define DEMO_SHOW_FLAG_LOSS 1

static const char *TAG = "motovis";
static const char *LED_TAG = "led";

typedef enum {
    LED_STATE_BOOTING = 0,
    LED_STATE_ADVERTISING,
    LED_STATE_CONNECTED,
    LED_STATE_ERROR,
} led_state_t;

static volatile led_state_t s_led_state = LED_STATE_BOOTING;

static uint32_t now_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000ULL);
}

static const char *led_state_name(led_state_t state)
{
    switch (state) {
        case LED_STATE_BOOTING:     return "booting";
        case LED_STATE_ADVERTISING: return "advertising";
        case LED_STATE_CONNECTED:   return "connected";
        case LED_STATE_ERROR:       return "error";
        default:                    return "unknown";
    }
}

// half-period of the blink in ms. 0 means hold it solid on
static uint32_t led_state_period_ms(led_state_t state)
{
    switch (state) {
        case LED_STATE_BOOTING:     return 100;
        case LED_STATE_ADVERTISING: return 500;
        case LED_STATE_CONNECTED:   return 0;
        case LED_STATE_ERROR:       return 1000;
        default:                    return 500;
    }
}

static void status_led_set_state(led_state_t state)
{
    s_led_state = state;
}

static void status_led_task(void *arg)
{
    bool led_on = false;
    int  last_logged = -1;

    while (1) {
        led_state_t state = s_led_state;

        if ((int) state != last_logged) {
            ESP_LOGI(LED_TAG, "state -> %s", led_state_name(state));
            last_logged = (int) state;
        }

        uint32_t period_ms = led_state_period_ms(state);

        if (period_ms == 0) {
            led_on = true;
        } else {
            led_on = !led_on;
        }

        gpio_set_level(STATUS_LED_GPIO, led_on);
        vTaskDelay(pdMS_TO_TICKS(period_ms == 0 ? 200 : period_ms));
    }
}

static const char *button_label(event_button_t b)
{
    return (b == EVENT_BUTTON_PAIRING) ? "pairing" : "alert-cancel";
}

void app_main(void)
{
    ESP_LOGI(TAG, "MotoVis jacket firmware starting");

    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_config));

    xTaskCreate(status_led_task, "status_led", 2048, NULL, 3, NULL);

    if (!event_bus_init()) {
        ESP_LOGE(TAG, "event bus failed to start");
        status_led_set_state(LED_STATE_ERROR);
    }

    esp_err_t err = battery_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "battery manager failed to start: %s", esp_err_to_name(err));
        status_led_set_state(LED_STATE_ERROR);
    } else {
        status_led_set_state(LED_STATE_ADVERTISING);
    }

    err = accel_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "accel manager failed to start: %s", esp_err_to_name(err));
        status_led_set_state(LED_STATE_ERROR);
    }

    err = gnss_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gnss manager failed to start: %s", esp_err_to_name(err));
    }

    err = heart_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "heart manager failed to start: %s", esp_err_to_name(err));
    }

    err = jacket_controls_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "jacket controls failed to start: %s", esp_err_to_name(err));
    }

    err = ble_server_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ble server failed to start: %s", esp_err_to_name(err));
        status_led_set_state(LED_STATE_ERROR);
    }

    battery_sample_t      battery;
    accel_reading_t       accel;
    gnss_reading_t        gps;
    heart_reading_t       heart;
    event_t bus_ev;

    uint32_t last_battery_seq = 0;
    uint32_t last_gps_seq     = 0;
    uint32_t last_beat_count  = 0;
#if DEMO_SHOW_FLAG_LOSS
    uint32_t last_alert_count = 0;
    uint32_t missed_total     = 0;
#endif
    bool     last_fix_valid   = false;

    alert_state_t alert;
    alert_state_init(&alert, ALERT_WINDOW_MS);

    while (1) {
        if (battery_manager_get_latest_sample(&battery) &&
            battery.sequence != last_battery_seq) {
            last_battery_seq = battery.sequence;
            ESP_LOGI(TAG, "battery: %u mV, %u%%",
                     (unsigned) battery.voltage_mv,
                     (unsigned) battery.percentage);

            // no raw adc in our battery manager, so that field stays 0
            motovis_battery_payload_t bp = {
                .timestamp_ms  = battery.timestamp_ms,
                .voltage_mv    = battery.voltage_mv,
                .raw_adc       = 0,
                .percentage    = battery.percentage,
                .battery_valid = battery.valid,
            };
            ble_server_publish(BLE_CHAR_BATTERY, &bp, sizeof(bp));
        }

        if (gnss_manager_get_latest(&gps) && gps.sequence != last_gps_seq) {
            last_gps_seq = gps.sequence;
            ESP_LOGI(TAG, "gps: lat=%ld lon=%ld sats=%u valid=%u",
                     (long) gps.lat_e7, (long) gps.lon_e7,
                     (unsigned) gps.satellites, (unsigned) gps.fix_valid);

            // the snapshot says what is true now. the bus says what changed
            if (gps.fix_valid != last_fix_valid) {
                last_fix_valid = gps.fix_valid;
                bus_ev.kind = last_fix_valid ? EVENT_FIX_GAINED : EVENT_FIX_LOST;
                event_bus_publish(&bus_ev);
            }

            motovis_gps_payload_t gp = {
                .timestamp_ms = gps.timestamp_ms,
                .latitude_e7  = gps.lat_e7,
                .longitude_e7 = gps.lon_e7,
                .fix_valid    = gps.fix_valid,
            };
            ble_server_publish(BLE_CHAR_GPS, &gp, sizeof(gp));
        }

        if (heart_manager_get_latest(&heart) &&
            heart.beat_count != last_beat_count) {
            uint32_t new_beats = heart.beat_count - last_beat_count;
            last_beat_count = heart.beat_count;
            ESP_LOGI(TAG, "heart: %u bpm (valid=%u), %lu new beat(s), %lu total",
                     (unsigned) heart.bpm, (unsigned) heart.bpm_valid,
                     (unsigned long) new_beats,
                     (unsigned long) heart.beat_count);

            motovis_heart_payload_t hp = {
                .timestamp_ms = heart.timestamp_ms,
                .bpm          = heart.bpm,
                .raw_adc      = heart.raw,
                .bpm_valid    = heart.bpm_valid,
            };
            ble_server_publish(BLE_CHAR_HEART, &hp, sizeof(hp));
        }

        if (accel_manager_get_latest(&accel)) {
#if DEMO_SHOW_FLAG_LOSS
            bool caught = (accel.alert != 0);

            uint32_t new_alerts = accel.alert_count - last_alert_count;
            last_alert_count = accel.alert_count;

            if (caught) {
                ESP_LOGW(TAG, "caught the alert flag, total=%lu",
                         (unsigned long) accel.alert_count);
            } else if (new_alerts > 0) {
                missed_total += new_alerts;
                ESP_LOGE(TAG, "MISSED %lu alert(s) - flag was already gone. "
                              "missed %lu of %lu so far",
                         (unsigned long) new_alerts,
                         (unsigned long) missed_total,
                         (unsigned long) accel.alert_count);
            }
#endif

            motovis_accel_payload_t apl = {
                .timestamp_ms = accel.timestamp_ms,
                .ax_mg        = accel.x_mg,
                .ay_mg        = accel.y_mg,
                .az_mg        = accel.z_mg,
                .alert        = accel.alert,
            };
            ble_server_publish(BLE_CHAR_ACCEL, &apl, sizeof(apl));
        }

        // one place to see everything that happened, in order
        while (event_bus_take(&bus_ev, 0)) {
            if (bus_ev.kind == EVENT_CRASH) {
                ESP_LOGW(TAG, "event: CRASH %d, %d, %d mg (seq %lu, at %lu ms)",
                         bus_ev.data.crash.x_mg,
                         bus_ev.data.crash.y_mg,
                         bus_ev.data.crash.z_mg,
                         (unsigned long) bus_ev.sequence,
                         (unsigned long) bus_ev.timestamp_ms);
            } else if (bus_ev.kind == EVENT_BUTTON) {
                ESP_LOGI(TAG, "event: button %s %s (seq %lu, at %lu ms)",
                         button_label(bus_ev.data.button.button),
                         bus_ev.data.button.pressed ? "pressed" : "released",
                         (unsigned long) bus_ev.sequence,
                         (unsigned long) bus_ev.timestamp_ms);
            } else {
                ESP_LOGI(TAG, "event: %s (seq %lu, at %lu ms, %lu dropped)",
                         event_kind_name(bus_ev.kind),
                         (unsigned long) bus_ev.sequence,
                         (unsigned long) bus_ev.timestamp_ms,
                         (unsigned long) event_bus_dropped());
            }

            // every event goes past the alert logic too
            if (alert_state_event(&alert, &bus_ev)) {
                ESP_LOGW(TAG, "alert: %s", alert_phase_name(alert.phase));
            }
        }

        // the window running out isn't an event, so we have to go looking
        if (alert_state_tick(&alert, now_ms())) {
            ESP_LOGE(TAG, "alert: CONFIRMED - would send %d, %d, %d mg "
                          "from %lu ms (%lu sent, %lu cancelled)",
                     alert.x_mg, alert.y_mg, alert.z_mg,
                     (unsigned long) alert.crash_at_ms,
                     (unsigned long) alert.confirmed_count,
                     (unsigned long) alert.cancelled_count);
        }

        if (alert.phase == ALERT_CONFIRMED || alert.phase == ALERT_CANCELLED) {
            // the value stays in the attribute table, so a phone that
            // connects later can still read what happened
            motovis_alert_payload_t alp = {
                .crash_at_ms  = alert.crash_at_ms,
                .latitude_e7  = gps.lat_e7,
                .longitude_e7 = gps.lon_e7,
                .ax_mg        = alert.x_mg,
                .ay_mg        = alert.y_mg,
                .az_mg        = alert.z_mg,
                .flags        = (uint8_t) ((gps.fix_valid ? MOTOVIS_ALERT_FLAG_FIX_VALID : 0) |
                                           (alert.phase == ALERT_CANCELLED
                                                ? MOTOVIS_ALERT_FLAG_CANCELLED : 0)),
                .sequence     = (uint8_t) (alert.confirmed_count + alert.cancelled_count),
            };
            ble_server_publish(BLE_CHAR_ALERT, &alp, sizeof(alp));

            // dealt with it, so let the next one through
            alert_state_ack(&alert);
        }

        // the led finally means something real
        if (s_led_state != LED_STATE_ERROR) {
            status_led_set_state(ble_server_connected()
                                     ? LED_STATE_CONNECTED : LED_STATE_ADVERTISING);
        }

        vTaskDelay(pdMS_TO_TICKS(400));
    }
}
