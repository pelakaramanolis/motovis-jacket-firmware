#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "gnss_uart.h"

#define GNSS_UART_PORT   UART_NUM_2
#define GNSS_TX_PIN      33
#define GNSS_RX_PIN      32
#define GNSS_BAUD        9600
#define GNSS_RX_BUF      1024

static const char *TAG = "gnss_uart";

esp_err_t gnss_uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = GNSS_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(GNSS_UART_PORT, GNSS_RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(GNSS_UART_PORT, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart config failed: %s", esp_err_to_name(err));
        uart_driver_delete(GNSS_UART_PORT);
        return err;
    }

    err = uart_set_pin(GNSS_UART_PORT, GNSS_TX_PIN, GNSS_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart pin assignment failed: %s", esp_err_to_name(err));
        uart_driver_delete(GNSS_UART_PORT);
        return err;
    }

    ESP_LOGI(TAG, "uart2 up: %d baud, tx=%d rx=%d",
             GNSS_BAUD, GNSS_TX_PIN, GNSS_RX_PIN);
    return ESP_OK;
}

int gnss_uart_read(uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    return uart_read_bytes(GNSS_UART_PORT, buf, len, pdMS_TO_TICKS(timeout_ms));
}
