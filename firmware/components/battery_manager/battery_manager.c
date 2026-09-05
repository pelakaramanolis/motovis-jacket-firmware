#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "battery_manager.h"

#define BATTERY_SAMPLE_INTERVAL_MS  1000
#define BATTERY_TASK_STACK_SIZE     2560
#define BATTERY_TASK_PRIORITY       4

#define BATTERY_MIN_VALID_MV        2500
#define BATTERY_MAX_VALID_MV        5000
#define BATTERY_EMPTY_MV            3300
#define BATTERY_FULL_MV             4200

static const char *TAG = "battery";

static SemaphoreHandle_t s_mutex       = NULL;
static bool              s_initialized = false;
static battery_sample_t  s_latest      = {0};

static uint32_t now_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000ULL);
}

static TickType_t ms_to_ticks_min1(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return (ticks == 0) ? 1 : ticks;
}

/*
 * Stand-in for the real measurement.
 * On the Feather V2 this reads the battery divider on GPIO35 and doubles it.
 * QEMU has no ADC, so this walks a simulated cell from full to empty and repeats.
 */
static uint16_t battery_read_mv(void)
{
    static uint16_t simulated_mv = BATTERY_FULL_MV;

    if (simulated_mv <= BATTERY_EMPTY_MV) {
        simulated_mv = BATTERY_FULL_MV;
    } else {
        simulated_mv -= 50;
    }

    return simulated_mv;
}

static uint8_t percent_from_mv(uint16_t mv)
{
    static const struct { uint16_t mv; uint8_t percent; } curve[] = {
        {4200, 100}, {4110, 90}, {4020, 80}, {3950, 70},
        {3870, 60},  {3790, 50}, {3740, 40}, {3680, 30},
        {3600, 20},  {3500, 10}, {3300, 0},
    };

    if (mv >= BATTERY_FULL_MV)  { return 100; }
    if (mv <= BATTERY_EMPTY_MV) { return 0; }

    for (size_t i = 0; i < (sizeof(curve) / sizeof(curve[0])) - 1U; i++) {
        uint16_t high_mv = curve[i].mv;
        uint16_t low_mv  = curve[i + 1].mv;

        if (mv <= high_mv && mv >= low_mv) {
            uint16_t mv_span      = high_mv - low_mv;
            uint16_t mv_offset    = mv - low_mv;
            uint8_t  percent_span = curve[i].percent - curve[i + 1].percent;
            return (uint8_t) (curve[i + 1].percent +
                              ((mv_offset * percent_span) / mv_span));
        }
    }

    return 0;
}

static void battery_store_sample(uint16_t mv)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    s_latest.timestamp_ms = now_ms();
    s_latest.voltage_mv   = mv;
    s_latest.valid        = (mv >= BATTERY_MIN_VALID_MV &&
                             mv <= BATTERY_MAX_VALID_MV) ? 1U : 0U;
    s_latest.percentage   = s_latest.valid ? percent_from_mv(mv) : 0U;
    s_latest.sequence    += 1U;

    xSemaphoreGive(s_mutex);
}

static void battery_task(void *arg)
{
    const TickType_t delay = ms_to_ticks_min1(BATTERY_SAMPLE_INTERVAL_MS);

    while (1) {
        battery_store_sample(battery_read_mv());
        vTaskDelay(delay);
    }
}

esp_err_t battery_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create battery mutex");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(battery_task, "battery", BATTERY_TASK_STACK_SIZE,
                    NULL, BATTERY_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create battery task");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "battery manager started (simulated source)");
    return ESP_OK;
}

bool battery_manager_get_latest_sample(battery_sample_t *out_sample)
{
    if (!s_initialized || out_sample == NULL || s_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    memcpy(out_sample, &s_latest, sizeof(*out_sample));
    xSemaphoreGive(s_mutex);
    return true;
}