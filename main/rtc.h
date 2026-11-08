#pragma once
#include "esp_err.h"
#include <time.h>

/* PCF85063A on the shared BSP I2C bus. All stored times are UTC. */
esp_err_t pet_rtc_init(void);
esp_err_t pet_rtc_read(time_t *utc);
esp_err_t pet_rtc_write(time_t utc);
