#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

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

/* Half-period of the blink, in ms. 0 means hold the LED solid on. */
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
    int last_logged = -1;

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

    /* Walk through the states so we can watch the transitions. */
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "pretending BLE started advertising");
    status_led_set_state(LED_STATE_ADVERTISING);

    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "pretending a phone connected");
    status_led_set_state(LED_STATE_CONNECTED);

    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "pretending something broke");
    status_led_set_state(LED_STATE_ERROR);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}