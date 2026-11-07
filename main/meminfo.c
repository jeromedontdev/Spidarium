/*
 * Spidarium — memory reporting, implementation.
 */
#include "meminfo.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "spidarium.mem";

/* Internal RAM is the scarce pool. It is what the LCD SPI driver needs for its
 * DMA buffers, what the WiFi stack wants for its pools, and what every task
 * stack comes from. PSRAM is plentiful (8 MB) but slower and not DMA-capable
 * for the LCD, so it is not a substitute. */
#define CAP_INTERNAL  (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define CAP_DMA       (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)
#define CAP_PSRAM     (MALLOC_CAP_SPIRAM  | MALLOC_CAP_8BIT)

void mem_report(const char *where)
{
    size_t int_free  = heap_caps_get_free_size(CAP_INTERNAL);
    size_t int_total = heap_caps_get_total_size(CAP_INTERNAL);
    size_t ps_free   = heap_caps_get_free_size(CAP_PSRAM);
    size_t ps_total  = heap_caps_get_total_size(CAP_PSRAM);

    ESP_LOGI(TAG, "%-14s internal %6u/%6u free   psram %7u/%7u free",
             where ? where : "",
             (unsigned)int_free, (unsigned)int_total,
             (unsigned)ps_free, (unsigned)ps_total);
}

void mem_report_detail(const char *where)
{
    size_t int_free   = heap_caps_get_free_size(CAP_INTERNAL);
    size_t int_largest = heap_caps_get_largest_free_block(CAP_INTERNAL);
    size_t dma_free   = heap_caps_get_free_size(CAP_DMA);
    size_t dma_largest = heap_caps_get_largest_free_block(CAP_DMA);
    size_t ps_free    = heap_caps_get_free_size(CAP_PSRAM);
    size_t ps_largest = heap_caps_get_largest_free_block(CAP_PSRAM);

    ESP_LOGI(TAG, "--- memory at %s ---", where ? where : "?");

    /* The largest block is the number that matters for the LCD. A pool can
     * report plenty free and still fail a 48 KB DMA allocation if it is
     * fragmented, which is exactly how the display corruption presented. */
    ESP_LOGI(TAG, "  internal  free %6u  largest %6u",
             (unsigned)int_free, (unsigned)int_largest);
    ESP_LOGI(TAG, "  dma-cap   free %6u  largest %6u",
             (unsigned)dma_free, (unsigned)dma_largest);
    ESP_LOGI(TAG, "  psram     free %7u  largest %7u",
             (unsigned)ps_free, (unsigned)ps_largest);

    /* A warning threshold, not a hard limit. Below ~32 KB of internal RAM the
     * LCD driver starts failing frames; below ~16 KB the system is unstable. */
    if (int_largest < 32u * 1024u) {
        ESP_LOGW(TAG, "  internal largest block is low (%u) -- LCD DMA may fail",
                 (unsigned)int_largest);
    }
}
