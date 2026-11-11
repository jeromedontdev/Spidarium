#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NET_SAVED_MAX 3

typedef enum {
    NET_OFF = 0,
    NET_CONNECTING,
    NET_CONNECTED,
    NET_FAILED,
} net_state_t;

/* Boot-only: restore RTC, load credentials, sync once if configured.
 * Call before starting the display. No credentials means no radio startup. */
void net_boot_init(void);
void net_display_ready(void);
void net_init(void);

/* All jobs return immediately. Results are published when busy becomes false.
 * Credentials are committed on the worker, never on the LVGL task. */
bool net_set_credentials(const char *ssid, const char *pass);
bool net_connect_to(const char *ssid, const char *pass);
bool net_sync_saved(int index);
bool net_forget_saved(int index);
bool net_ota_start(const char *https_url);
bool net_ota_latest(void);
bool net_ota_test(void);
int net_saved_count(void);
const char *net_saved_ssid(int index);
bool net_has_credentials(void);
const char *net_ssid(void);
net_state_t net_state(void);
const char *net_status_text(void);
const char *net_ip(void);
void net_tick(uint32_t now_ms);

bool net_scan_start(void);
bool net_busy(void);
bool net_radio_active(void);
uint32_t net_generation(void);
int net_scan_count(void);
const char *net_scan_error(void);
const char *net_scan_ssid(int index);
int net_scan_rssi(int index);
bool net_scan_secure(int index);
