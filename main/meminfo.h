/*
 * Spidarium — memory reporting.
 *
 * Prints where the RAM actually went, at boot and on demand. This exists
 * because three separate bugs in this project were memory shortages that
 * presented as something else:
 *
 *   - SD probing at boot stole DMA buffers from the LCD driver
 *   - WiFi's RX/TX pools starved the same pool
 *   - LVGL's draw buffers competed with both
 *
 * Each one showed up as "Draw bitmap failed: ESP_ERR_NO_MEM" and a corrupted
 * screen, which says nothing about the cause. A written budget plus this report
 * makes the shortage visible before it becomes a bug.
 *
 * See docs/memory_budget.md for the numbers and the reasoning.
 */
#pragma once

#include <stdint.h>

/* Log a one-line summary of internal and PSRAM usage. */
void mem_report(const char *where);

/* Log a detailed breakdown, including the largest free block in each pool.
 * The largest block matters more than the total: a fragmented pool can have
 * plenty free and still fail a single large allocation, which is exactly what
 * the LCD DMA buffers are. */
void mem_report_detail(const char *where);
