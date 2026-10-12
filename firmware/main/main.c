#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include "accel_manager.h"
#include "battery_manager.h"
#include "gnss_manager.h"

#define STATUS_LED_GPIO 2

static const char *TAG = "motovis";
static const char *LED_TAG = "led";

typedef enum {
    LED_STATE_BOOTING = 0,
    LED_STATE_ADVERTISING,
    LED_STATE_CONNECTED,
    LED_STATE_ERROR,
} led_state_t;

static volatile led_state_t s_led_state = LED_STATE_BOOTING;

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

    battery_sample_t battery;
    accel_reading_t  accel;
    gnss_reading_t   gps;

    uint32_t last_battery_seq = 0;
    uint32_t last_gps_seq     = 0;
    uint32_t last_alert_count = 0;
    uint32_t missed_total     = 0;

    while (1) {
        if (battery_manager_get_latest_sample(&battery) &&
            battery.sequence != last_battery_seq) {
            last_battery_seq = battery.sequence;
            ESP_LOGI(TAG, "battery: %u mV, %u%%",
                     (unsigned) battery.voltage_mv,
                     (unsigned) battery.percentage);
        }

        if (gnss_manager_get_latest(&gps) && gps.sequence != last_gps_seq) {
            last_gps_seq = gps.sequence;
            ESP_LOGI(TAG, "gps: lat=%ld lon=%ld sats=%u valid=%u",
                     (long) gps.lat_e7, (long) gps.lon_e7,
                     (unsigned) gps.satellites, (unsigned) gps.fix_valid);
        }

        if (accel_manager_get_latest(&accel)) {
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
        }

        vTaskDelay(pdMS_TO_TICKS(400));
    }
}
