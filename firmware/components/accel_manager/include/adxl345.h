#ifndef ADXL345_H
#define ADXL345_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// sets up i2c, checks the chip is actually there, and puts it in measure mode
esp_err_t adxl345_init(void);

// reads all three axes in one go. values come back in milli-g
esp_err_t adxl345_read_mg(int16_t *x_mg, int16_t *y_mg, int16_t *z_mg);

#endif /*ADXL345_H*/