#include "event_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static QueueHandle_t     s_queue;
static SemaphoreHandle_t s_lock;
static uint32_t          s_sequence;
static volatile uint32_t s_dropped;

bool event_bus_init(void)
{
    // if someone calls init twice we don't want two buses
    if (s_queue != NULL) {
        return true;
    }

    s_queue = xQueueCreate(EVENT_BUS_DEPTH, sizeof(event_t));
    s_lock  = xSemaphoreCreateMutex();

    return s_queue != NULL && s_lock != NULL;
}

bool event_bus_publish(event_t *ev)
{
    if (s_queue == NULL || ev == NULL) {
        return false;
    }

    // stamping and queueing have to happen together, see notes
    xSemaphoreTake(s_lock, portMAX_DELAY);

    ev->timestamp_ms = (uint32_t) (esp_timer_get_time() / 1000);
    ev->sequence     = ++s_sequence;

    bool sent = xQueueSend(s_queue, ev, 0) == pdTRUE;
    if (!sent) {
        s_dropped++;
    }

    xSemaphoreGive(s_lock);
    return sent;
}

bool event_bus_take(event_t *out, uint32_t wait_ms)
{
    if (s_queue == NULL || out == NULL) {
        return false;
    }

    TickType_t ticks = (wait_ms == 0) ? 0 : pdMS_TO_TICKS(wait_ms);
    return xQueueReceive(s_queue, out, ticks) == pdTRUE;
}

uint32_t event_bus_dropped(void)
{
    return s_dropped;
}

const char *event_kind_name(event_kind_t kind)
{
    switch (kind) {
    case EVENT_CRASH:       return "crash";
    case EVENT_BUTTON:      return "button";
    case EVENT_FIX_GAINED:  return "gps fix";
    case EVENT_FIX_LOST:    return "gps lost";
    case EVENT_BATTERY_LOW: return "battery low";
    }
    return "unknown";
}
