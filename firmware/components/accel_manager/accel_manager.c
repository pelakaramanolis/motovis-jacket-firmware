#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "accel_manager.h"
#include "adxl345.h"
#include "crash_detect.h"

// qemu has no i2c device to talk to, so we feed the detector a scripted motion pattern instead

#define ACCEL_USE_SIMULATED     1

#define ACCEL_SAMPLE_MS         100     // 10 hz, same as the original
#define ACCEL_TASK_STACK        3072
#define ACCEL_TASK_PRIORITY     4

#define CRASH_THRESHOLD_MG      500
#define CRASH_REFRACTORY_MS     2000

static const char *TAG = "accel";

static SemaphoreHandle_t s_mutex = NULL;
static bool              s_ready = false;
static accel_reading_t   s_latest = {0};
static crash_detector_t  s_detector;

static uint32_t now_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000ULL);
}

static TickType_t ms_to_ticks_min1(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    return (t == 0) ? 1 : t;
}

#if ACCEL_USE_SIMULATED

// 60 steps at 10 hz so it loops every 6 seconds

static void simulated_read_mg(uint32_t step, int16_t *x, int16_t *y, int16_t *z)
{
    static const int16_t lean[4][3] = {
        { 707, 0,  707 },   // 45 degrees over
        {1000, 0,    0 },   // fully leaned
        { 707, 0,  707 },   // coming back
        {   0, 0, 1000 },   // upright again
    };
    static const int16_t gait[8] = { 1000, 1100, 1150, 1100, 1000, 900, 850, 900 };

    uint32_t phase = step % 60;

    if (phase < 20) {                  // sitting still
        *x = 0; *y = 0; *z = 1000;
    } else if (phase < 24) {           // quick lean
        *x = lean[phase - 20][0];
        *y = lean[phase - 20][1];
        *z = lean[phase - 20][2];
    } else if (phase < 45) {           // walking
        *x = 0; *y = 0; *z = gait[(phase - 24) % 8];
    } else if (phase < 48) {           // the crash
        *x = 5000; *y = 0; *z = 1000;
    } else {                           // still again afterwards
        *x = 0; *y = 0; *z = 1000;
    }
}

#endif

static void store(int16_t x, int16_t y, int16_t z, bool alert)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    s_latest.timestamp_ms = now_ms();
    s_latest.x_mg         = x;
    s_latest.y_mg         = y;
    s_latest.z_mg         = z;
    s_latest.alert        = alert ? 1U : 0U;
    s_latest.sequence    += 1U;

    
    if (alert) {
        s_latest.alert_count += 1U;
    }

    xSemaphoreGive(s_mutex);
}

static void accel_task(void *arg)
{
    const TickType_t delay = ms_to_ticks_min1(ACCEL_SAMPLE_MS);
    uint32_t step = 0;

    while (1) {
        int16_t x = 0, y = 0, z = 0;
        bool ok = true;

#if ACCEL_USE_SIMULATED
        simulated_read_mg(step, &x, &y, &z);
#else
        ok = (adxl345_read_mg(&x, &y, &z) == ESP_OK);
#endif

        if (ok) {
            accel_sample_t s = {
                .t_ms = now_ms(),
                .x_mg = x,
                .y_mg = y,
                .z_mg = z,
            };

            bool alert = crash_detector_update(&s_detector, &s);

            if (alert) {
                ESP_LOGW(TAG, "crash detected: %d, %d, %d mg", x, y, z);
            }

            store(x, y, z, alert);
        } else {
            ESP_LOGW(TAG, "couldn't read the accelerometer");
        }

        step++;
        vTaskDelay(delay);
    }
}

esp_err_t accel_manager_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "couldn't make the accel mutex");
        return ESP_ERR_NO_MEM;
    }

#if !ACCEL_USE_SIMULATED
    esp_err_t err = adxl345_init();
    if (err != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return err;
    }
#endif

    crash_detector_init(&s_detector, CRASH_THRESHOLD_MG, CRASH_REFRACTORY_MS);

    if (xTaskCreate(accel_task, "accel", ACCEL_TASK_STACK,
                    NULL, ACCEL_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "couldn't start the accel task");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(TAG, "accel manager running%s",
             ACCEL_USE_SIMULATED ? " (simulated motion)" : "");
    return ESP_OK;
}

bool accel_manager_get_latest(accel_reading_t *out)
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