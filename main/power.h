#pragma once

#include <stdbool.h>
#include <stdint.h>

bool power_init(void);
void power_tick(void);
bool power_sleeping(void);
bool power_writing(void);
typedef struct {
    bool valid, present, usb, charging;
    uint16_t millivolts;
    uint8_t percent; /* 127 means unknown. */
} power_reading_t;
power_reading_t power_reading(void);
void power_status(char *text, unsigned size);
bool power_test(void);
bool power_ui_test(void);
void power_wake(void);
