#ifndef JACKET_CONTROLS_H
#define JACKET_CONTROLS_H

#include "esp_err.h"

// button presses come out of the event bus now. see event_bus.h
esp_err_t jacket_controls_init(void);

#endif // JACKET_CONTROLS_H
