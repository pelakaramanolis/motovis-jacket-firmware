#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "gnss_manager.h"
#include "gnss_uart.h"
#include "nmea.h"

// qemu has no gps module, so we generate real nmea text and feed it through
// the same parser. flip to 0 on hardware
#define GNSS_USE_SIMULATED  1

#define GNSS_INTERVAL_MS    1000
#define GNSS_TASK_STACK     3584
#define GNSS_TASK_PRIORITY  4

static const char *TAG = "gnss";

static SemaphoreHandle_t s_mutex  = NULL;
static bool              s_ready  = false;
static gnss_reading_t    s_latest = {0};

static uint32_t now_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000ULL);
}

static TickType_t ms_to_ticks_min1(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    return (t == 0) ? 1 : t;
}

#if GNSS_USE_SIMULATED

// first few seconds report no fix, like a real cold start, then a position
// that creeps north
static void build_sentence(char *out, size_t cap, uint32_t step)
{
    char body[96];

    if (step < 5) {
        snprintf(body, sizeof(body), "GPGGA,123519,,,,,0,00,,,M,,M,,");
    } else {
        unsigned long frac = (38UL + (step - 5UL)) % 1000UL;
        snprintf(body, sizeof(body),
                 "GPGGA,123519,4807.%03lu,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,",
                 frac);
    }

    uint8_t sum = 0;
    for (const char *p = body; *p != '\0'; p++) {
        sum ^= (uint8_t) *p;
    }

    snprintf(out, cap, "$%s*%02X\r\n", body, sum);
}

#endif

static void store(const nmea_fix_t *fix)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    s_latest.timestamp_ms = now_ms();
    s_latest.lat_e7       = fix->lat_e7;
    s_latest.lon_e7       = fix->lon_e7;
    s_latest.satellites   = fix->satellites;
    s_latest.fix_valid    = fix->valid ? 1U : 0U;
    s_latest.sequence    += 1U;

    xSemaphoreGive(s_mutex);
}

static void gnss_task(void *arg)
{
    nmea_parser_t parser;
    nmea_init(&parser);

#if GNSS_USE_SIMULATED
    uint32_t step = 0;

    while (1) {
        char sentence[160];
        build_sentence(sentence, sizeof(sentence), step);

        for (const char *c = sentence; *c != '\0'; c++) {
            nmea_fix_t fix;
            if (nmea_feed(&parser, *c, &fix)) {
                store(&fix);
            }
        }

        step++;
        vTaskDelay(ms_to_ticks_min1(GNSS_INTERVAL_MS));
    }
#else
    uint8_t buf[128];

    while (1) {
        int n = gnss_uart_read(buf, sizeof(buf), 200);
        if (n < 0) {
            ESP_LOGW(TAG, "uart read failed");
            continue;
        }

        for (int i = 0; i < n; i++) {
            nmea_fix_t fix;
            if (nmea_feed(&parser, (char) buf[i], &fix)) {
                store(&fix);
            }
        }
    }
#endif
}

esp_err_t gnss_manager_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "couldn't make the gnss mutex");
        return ESP_ERR_NO_MEM;
    }

#if !GNSS_USE_SIMULATED
    esp_err_t err = gnss_uart_init();
    if (err != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return err;
    }
#endif

    if (xTaskCreate(gnss_task, "gnss", GNSS_TASK_STACK,
                    NULL, GNSS_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "couldn't start the gnss task");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(TAG, "gnss manager running%s",
             GNSS_USE_SIMULATED ? " (simulated sentences)" : "");
    return ESP_OK;
}

bool gnss_manager_get_latest(gnss_reading_t *out)
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
