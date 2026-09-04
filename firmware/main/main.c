#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define STATUS_LED_GPIO 2

static const char *TAG = "motovis";

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

    bool led_on = false;

    while (1) {
        led_on = !led_on;
        gpio_set_level(STATUS_LED_GPIO, led_on);
        ESP_LOGI(TAG, "led %s", led_on ? "on" : "off");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}