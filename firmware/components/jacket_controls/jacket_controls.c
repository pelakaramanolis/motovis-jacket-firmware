#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "event_bus.h"

#include "debounce.h"
#include "jacket_controls.h"

// qemu has no buttons to press, so we synthesise a bouncy raw signal and
// run it through the real debouncer
#define CONTROLS_USE_SIMULATED  1

#define PAIRING_GPIO        25
#define ALERT_CANCEL_GPIO   26

#define BUTTON_COUNT        2
#define POLL_MS             5
#define SETTLE_MS           30
#define TASK_STACK          3072
#define TASK_PRIORITY       5

static const char *TAG = "controls";

static bool       s_ready = false;
static debounce_t s_debounce[BUTTON_COUNT];

static uint32_t now_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000ULL);
}

static TickType_t ms_to_ticks_min1(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    return (t == 0) ? 1 : t;
}

static const char *button_name(event_button_t b)
{
    return (b == EVENT_BUTTON_PAIRING) ? "pairing" : "alert-cancel";
}

#if CONTROLS_USE_SIMULATED

// each button gets pressed for 200 ms on its own cycle, with about 20 ms of
// contact bounce at each edge
static bool simulated_raw(event_button_t b, uint32_t t_ms)
{
    uint32_t period = (b == EVENT_BUTTON_PAIRING) ? 7000U : 11000U;
    uint32_t phase  = t_ms % period;

    if (phase < 20)  { return ((phase / 3U) % 2U) == 0U; }
    if (phase < 200) { return true; }
    if (phase < 220) { return ((phase / 3U) % 2U) == 0U; }
    return false;
}

#else

static const gpio_num_t s_pins[BUTTON_COUNT] = {
    PAIRING_GPIO,
    ALERT_CANCEL_GPIO,
};

#endif

// no timestamp and no sequence number in here. the bus owns both now
static void push_event(event_button_t b, bool pressed)
{
    event_t ev = {
        .kind        = EVENT_BUTTON,
        .data.button = { .button = b, .pressed = pressed },
    };

    if (!event_bus_publish(&ev)) {
        ESP_LOGW(TAG, "bus was full, lost a %s event", button_name(b));
    }
}

static void controls_task(void *arg)
{
    const TickType_t delay = ms_to_ticks_min1(POLL_MS);

    while (1) {
        uint32_t t = now_ms();

        for (int i = 0; i < BUTTON_COUNT; i++) {
            bool raw;

#if CONTROLS_USE_SIMULATED
            raw = simulated_raw((event_button_t) i, t);
#else
            // buttons pull the pin to ground, so a low level means pressed
            raw = (gpio_get_level(s_pins[i]) == 0);
#endif

            if (debounce_update(&s_debounce[i], raw, t)) {
                push_event((event_button_t) i, s_debounce[i].stable);
            }
        }

        vTaskDelay(delay);
    }
}

esp_err_t jacket_controls_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    // safe to call twice, and it means we don't care who starts first
    if (!event_bus_init()) {
        ESP_LOGE(TAG, "event bus isn't up");
        return ESP_ERR_INVALID_STATE;
    }

#if !CONTROLS_USE_SIMULATED
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PAIRING_GPIO) | (1ULL << ALERT_CANCEL_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "button gpio config failed: %s", esp_err_to_name(err));
        return err;
    }
#endif

    for (int i = 0; i < BUTTON_COUNT; i++) {
        debounce_init(&s_debounce[i], SETTLE_MS, false);
    }

    if (xTaskCreate(controls_task, "controls", TASK_STACK,
                    NULL, TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "couldn't start the controls task");
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(TAG, "jacket controls running%s",
             CONTROLS_USE_SIMULATED ? " (simulated buttons)" : "");
    return ESP_OK;
}
