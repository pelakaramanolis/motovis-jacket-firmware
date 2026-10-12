#ifndef HEART_ADC_H
#define HEART_ADC_H

#include <stdint.h>
#include "esp_err.h"

esp_err_t heart_adc_init(void);
esp_err_t heart_adc_read(uint16_t *raw);

#endif // HEART_ADC_H