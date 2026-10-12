#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "heart_adc.h"
#include "heart_manager.h"
#include "pulse_detect.h"

// qemu has no adc, so we synthesise a pulse waveform instead
#define HEART_USE_SIMULATED  1

#define HEART_SAMPLE_MS      10     // 100 hz
#define HEART_TASK_STACK     3072
#define HEART_TASK_PRIORITY  4

#define HEART_REFRACTORY_MS  300
#define HEART_WINDOW_MS      2000

#define SIM_BPM              72
#define SIM_BASELINE         1800
#define SIM_AMPLITUDE        400

static const char *TAG = "heart";

static SemaphoreHandle_t s_mutex  = NULL;
static bool              s_ready  = false;
static heart_reading_t   s_latest = {0};
static pulse_detector_t  s_detector;

static uint32_t now_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000ULL);
}

static TickType_t ms_to_ticks_min1(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    return (t == 0) ? 1 : t;
}

#if HEART_USE_SIMULATED

// same waveform shape as the host tests: pressure wave then dicrotic notch,
// scaled to integer percent so there's no floating point on the chip
static uint16_t simulated_sample(uint32_t t_ms)
{
    uint32_t period = 60000U / SIM_BPM;
    uint32_t phase  = ((t_ms % period) * 100U) / period;   // 0..99

    uint32_t level;   // 0..100 percent of amplitude

    if (phase < 5)       { level = 0; }
    else if (phase < 15) { level = ((phase - 5) * 100U) / 10U; }
    else if (phase < 35) { level = 100U - ((phase - 15) * 100U) / 20U; }
    else if (phase < 45) { level = (40U * (phase - 35)) / 10U; }
    else if (phase < 60) { level = 40U - (40U * (phase - 45)) / 15U; }
    else                 { level = 0; }

    return (uint16_t) (SIM_BASELINE + (level * SIM_AMPLITUDE) / 100U);
}

#endif

static void store(uint16_t raw, const pulse_result_t *r)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    s_latest.timestamp_ms = now_ms();
    s_latest.raw          = raw;
    s_latest.bpm          = r->bpm;
    s_latest.bpm_valid    = r->bpm_valid ? 1U : 0U;
    s_latest.sequence    += 1U;

    if (r->beat) {
        s_latest.beat_count += 1U;
    }

    xSemaphoreGive(s_mutex);
}

static void heart_task(void *arg)
{
    const TickType_t delay = ms_to_ticks_min1(HEART_SAMPLE_MS);

    while (1) {
        uint32_t t   = now_ms();
        uint16_t raw = 0;
        bool     ok  = true;

#if HEART_USE_SIMULATED
        raw = simulated_sample(t);
#else
        ok = (heart_adc_read(&raw) == ESP_OK);
#endif

        if (ok) {
            pulse_result_t r = pulse_detector_update(&s_detector, raw, t);
            store(raw, &r);
        } else {
            ESP_LOGW(TAG, "couldn't read the pulse sensor");
        }

        vTaskDelay(delay);
    }
}

esp_err_t heart_manager_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "couldn't make the heart mutex");
        return ESP_ERR_NO_MEM;
    }

#if !HEART_USE_SIMULATED
    esp_err_t err = heart_adc_init();
    if (err != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return err;
    }
#endif

    pulse_detector_init(&s_detector, HEART_REFRACTORY_MS, HEART_WINDOW_MS);

    if (xTaskCreate(heart_task, "heart", HEART_TASK_STACK,
                    NULL, HEART_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "couldn't start the heart task");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(TAG, "heart manager running%s",
             HEART_USE_SIMULATED ? " (simulated pulse)" : "");
    return ESP_OK;
}

bool heart_manager_get_latest(heart_reading_t *out)
{
    if (!s_ready || out == NULL || s_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    memcpy(out, &s_latest, sizeof(*out));
    xSemaphoreGive(s_mutex);
    return true;
}
