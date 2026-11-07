/*
 * Spidarium — WiFi station connection.
 *
 * This exists so the device can reach the network for OTA updates. It is
 * deliberately minimal: connect to one saved network, report status, and stay
 * out of the way. There is no AP mode, no scanning UI, and no captive portal.
 *
 * Credentials live in NVS under their own namespace, separate from the game
 * save, so a factory reset of the spider does not forget the network.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    NET_OFF = 0,      /* not started */
    NET_CONNECTING,   /* trying to associate */
    NET_CONNECTED,    /* got an IP */
    NET_FAILED,       /* gave up; see net_status_text() */
} net_state_t;

/* Start the WiFi stack and try to connect with the saved credentials. Safe to
 * call when no credentials are saved: it simply stays in NET_OFF. */
void net_init(void);

/* Save credentials and (re)connect. Passing an empty SSID clears them. */
void net_set_credentials(const char *ssid, const char *pass);

/* True if credentials are stored. */
bool net_has_credentials(void);

/* The saved SSID, or "" if none. Never NULL. */
const char *net_ssid(void);

/* Current state. */
net_state_t net_state(void);

/* A short human-readable status for the settings menu: "off", "connecting",
 * "connected 192.168.1.42", or "failed". Never NULL. */
const char *net_status_text(void);

/* The assigned IP, or "" if not connected. Never NULL. */
const char *net_ip(void);

/* Call from the logic tick. Drives reconnect backoff; does nothing when
 * connected or when no credentials are saved. */
void net_tick(uint32_t now_ms);

/* ------------------------------------------------------------------ */
/* Scanning                                                            */
/* ------------------------------------------------------------------ */
/* Start a scan. Results arrive asynchronously; poll net_scan_count() until it
 * is non-zero. Returns false if the radio is not ready. */
bool net_scan_start(void);

/* True while a radio job is running. The UI polls this instead of waiting:
 * blocking the LVGL task hangs the display and trips the watchdog. */
bool net_busy(void);

/* True while the radio is on or about to be turned on.
 *
 * The tank must not be drawn while this is true: the radio holds internal
 * DMA-capable RAM that the LCD driver needs for its draw buffers, and drawing
 * at the same time is what corrupted the screen. */
bool net_radio_active(void);

/* Increments each time a queued radio job finishes. The UI compares this
 * against the value it last saw to know when to rebuild the WiFi menu. */
uint32_t net_generation(void);


/* Number of networks found by the last completed scan, or 0 while scanning. */
int net_scan_count(void);

/* Why the last scan failed, or "" on success. */
const char *net_scan_error(void);

/* SSID of result `i`, or "" if out of range. Never NULL. */
const char *net_scan_ssid(int i);

/* Signal strength of result `i` in dBm, or 0 if out of range. */
int net_scan_rssi(int i);

/* True if result `i` is secured (needs a password). */
bool net_scan_secure(int i);

/* Connect to a scanned network. `pass` may be "" for an open network. */
void net_connect_to(const char *ssid, const char *pass);
