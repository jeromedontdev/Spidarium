/*
 * Minimal esp_log stub for the headless simulator.
 *
 * The logic modules log through ESP_LOGI/W/E. On the host those become printf
 * with a level prefix, so the simulator's output shows the same messages the
 * device would emit -- which is most of the value of running it.
 */
#pragma once

#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) \
    printf("E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) \
    printf("W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) \
    printf("I %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) \
    do { (void)(tag); } while (0)
#define ESP_LOGV(tag, fmt, ...) \
    do { (void)(tag); } while (0)
