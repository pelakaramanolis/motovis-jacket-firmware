#ifndef GNSS_UART_H
#define GNSS_UART_H

#include <stdint.h>
#include "esp_err.h"

esp_err_t gnss_uart_init(void);

// blocks up to timeout_ms, returns bytes read, or -1 on error
int gnss_uart_read(uint8_t *buf, uint32_t len, uint32_t timeout_ms);

#endif // GNSS_UART_H