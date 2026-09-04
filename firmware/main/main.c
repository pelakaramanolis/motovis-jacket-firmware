#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "motovis";

void app_main(void)
{
    ESP_LOGI(TAG, "MotoVis jacket firmware starting");

    while (1) {
        ESP_LOGI(TAG, "alive");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}