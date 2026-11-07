# WiFi Architecture

**Author**: Copilot  
**Last Updated**: 2026-11-07  
**Status**: Specification (Ready for Implementation)

---

## Overview

WiFi should only be active when the user explicitly enables it, with support for multiple saved networks and manual selection before connecting.

### Design Principles

1. **User Control**: WiFi is OFF by default; user must enable it
2. **Multi-Network**: Save up to 3 networks, select which to connect to
3. **Non-Blocking**: All WiFi work runs on a dedicated task (already implemented)
4. **Radio-Off Strategy**: Radio is only active during scan/connect (existing architecture)
5. **Resilient**: Graceful failure recovery with visible error messages
6. **Secure**: Passwords stored in NVS with no plaintext in logs

---

## State Machine

### States

```c
typedef enum {
    NET_OFF,           // Radio is down, not initialized
    NET_IDLE,          // Radio is up but not associated
    NET_SCANNING,      // Active scan in progress
    NET_CONNECTING,    // Attempting to associate
    NET_CONNECTED,     // Associated with IP address
    NET_ERROR,         // Last operation failed (error string in display)
} net_state_t;
```

### Transitions

```
    ┌──────────────────────────────────┐
    │  OFF                             │
    │  (radio down, no netif)          │
    └──────────────────────────────────┘
                  │
                  │ user enables WiFi
                  ↓
    ┌──────────────────────────────────┐
    │  IDLE                            │
    │  (radio up, not associated)      │
    └──────────────────────────────────┘
                  │
        ┌─────────┼─────────┐
        │         │         │
        ↓         ↓         ↓
      SCAN    CONNECT    disabled
        │         │         │
        └────┬────┴─────────┘
             │
        (error) ──────→ ERROR
             │           │
             └───────────┘
                  │
        (retry from IDLE)
                  │
             ┌────┴─────────┐
             │              │
             ↓              ↓
         (success)      (error)
             │              │
             ↓              ↓
         CONNECTED        ERROR


    CONNECTED
        │
        ├─ user disables WiFi → OFF (teardown)
        └─ connection lost → IDLE (reconnect?)
```

---

## Storage

### NVS Namespace: `"spidarium_wifi"`

```
Key                 Type        Range       Purpose
─────────────────────────────────────────────────────────
enabled             u8          0/1         WiFi on/off toggle
auto_connect        u8          0/1         Auto-reconnect when WiFi enabled
num_networks        u8          0-3         Count of saved networks
current_network     u8          0-2         Index of last connected (or attempt)

net_0_ssid          string[33]              SSID of network 0
net_0_pass          string[65]              Password of network 0

net_1_ssid          string[33]              SSID of network 1
net_1_pass          string[65]              Password of network 1

net_2_ssid          string[33]              SSID of network 2
net_2_pass          string[65]              Password of network 2

scan_result_count   u8          0-20        Count of networks in last scan
scan_result_*       BLOB                    Serialized scan results
```

---

## API

### Public Functions

```c
// ========== State query ==========

net_state_t net_state(void);
bool net_enabled(void);
bool net_busy(void);
bool net_connected(void);

// Status display strings (for UI)
const char *net_state_string(void);
const char *net_ssid(void);              // Currently connected SSID
const char *net_ip(void);                // IP address
const char *net_error_string(void);      // Last error message

// ========== User actions ==========

// Toggle WiFi on/off
void net_set_enabled(bool enabled);

// Start a scan (async, returns immediately)
// Scan results available via net_scan_results() after completion
void net_scan_start(void);

// Get scan results
typedef struct {
    char ssid[33];
    int8_t rssi;              // Signal strength (-120 to 0 dBm)
    uint8_t auth_mode;        // WiFi auth type
} net_scan_result_t;

int net_scan_count(void);
const net_scan_result_t *net_scan_results(void);

// Network credential management
void net_add_network(const char *ssid, const char *password);
void net_remove_network(uint8_t index);
int net_saved_count(void);
const char *net_saved_ssid(uint8_t index);

// Select a saved network to connect to
void net_connect_saved(uint8_t index);

// Connect to a network (SSID + password, may not save)
void net_connect_manual(const char *ssid, const char *password);

// Auto-reconnect settings
bool net_auto_reconnect_enabled(void);
void net_set_auto_reconnect(bool enabled);

// ========== Monitoring (for menu refresh) ==========

// Generation counter increments every time state changes
uint32_t net_generation(void);

// True while any WiFi operation is in progress
bool net_busy(void);

// Callback when scan completes (for menu refresh)
typedef void (*net_scan_done_fn)(void);
void net_set_scan_done_callback(net_scan_done_fn fn);
```

### Serial Console Commands

```
wifi status                 # Show current state
wifi on                     # Enable WiFi
wifi off                    # Disable WiFi
wifi scan                   # Start scan (wait for completion)
wifi list                   # List saved networks
wifi add SSID PASSWORD      # Add/update network
wifi remove INDEX           # Remove network by index
wifi connect INDEX          # Connect to saved network (0-2)
wifi connect_manual SSID PASS  # Connect to any network
wifi auto_reconnect on|off  # Auto-reconnect setting
wifi error                  # Show last error
```

---

## Credential Security

### Storage

- Passwords stored in NVS (encrypted by ESP-IDF's built-in NVS encryption)
- **NO** plaintext passwords in logs or console output
- **NO** password echo in serial commands
- Passwords limited to 64 characters (WiFi standard: max 63 + NUL)

### Validation

- SSID length: 1-32 characters
- Password length: 0 (open) or 8-63 characters
- Auth modes supported: OPEN, WPA2, WPA3

### Logging

```c
// GOOD:
ESP_LOGI(TAG, "connected to SSID");

// BAD:
ESP_LOGI(TAG, "connected to SSID with password %s", password);

// GOOD:
ESP_LOGI(TAG, "stored network %d", index);

// BAD:
ESP_LOGI(TAG, "stored network: ssid=%s pass=%s", ssid, pass);
```

---

## Implementation Plan

### Phase 1: Refactor State Machine

**File: `main/net.c`**

1. Add `net_state_t` enum and global state variable
2. Rewrite state transitions as switch statements
3. Add `net_state_string()` for UI display
4. Add error tracking: `s_error_message` buffer
5. Log all state transitions

**Current issue**: No clear state tracking; WiFi is "on" or "off" but no distinction between IDLE and CONNECTED

### Phase 2: Credential Management

**Files: `main/net.c`, `main/net.h`**

1. Expand NVS schema (see Storage section above)
2. `creds_load()` → Load all 3 networks
3. `creds_save()` → Save any network
4. Add network list management: add/remove/update
5. Add functions: `net_saved_ssid()`, `net_saved_count()`
6. Validation: SSID length, password length, auth mode

### Phase 3: Scan & Connect

**Files: `main/net.c`**

1. Implement `net_scan_start()` → Queue job, return immediately
2. Scan results stored in temp array `s_scan_results[20]`
3. Implement `net_scan_results()` → Return results + count
4. Implement `net_connect_saved()` → Select from saved list
5. Implement `net_connect_manual()` → Connect to any SSID/password

### Phase 4: UI Integration

**Files: `main/ui_hud.c`, `main/ui_menus_def.c`**

1. Add WiFi menu (see Menu Architecture doc)
2. WiFi toggle on/off
3. Scan Networks → triggers scan, shows results, lets user select
4. Saved Networks → list to view/edit/delete
5. Add Manual Network → keyboard input (if UI supports)
6. Display current state and error messages

### Phase 5: Testing & Validation

**Commands to test:**
```powershell
python cmd.py COM7 "wifi status"
python cmd.py COM7 "wifi on"
python cmd.py COM7 "wifi scan"
python cmd.py COM7 "wifi add MyNetwork MyPassword123"
python cmd.py COM7 "wifi list"
python cmd.py COM7 "wifi connect 0"
python cmd.py COM7 "wifi off"
python cmd.py COM7 "scanloop 5"   # Check for DMA leaks
```

---

## Backward Compatibility

### Current Code That Must Keep Working

```c
net_state_t net_state();           // Already exists
bool net_busy();                   // Already exists
uint32_t net_generation();         // Already exists
bool net_radio_active();           // Already exists
```

### New Code Replaces

```c
// OLD:
net_scan_start();
net_state; // was just NET_OFF or NET_CONNECTED

// NEW:
net_state_t state = net_state();
const char *status = net_state_string();
```

---

## Testing Checklist

- [ ] WiFi starts disabled
- [ ] Toggling WiFi on/off works repeatedly
- [ ] Scan finds networks
- [ ] Networks sorted by signal strength
- [ ] Can add network (SSID + password)
- [ ] Can remove network
- [ ] Can connect to saved network
- [ ] Connection updates net_connected() and net_ssid()
- [ ] Error messages display on connection failure
- [ ] Auto-reconnect works when enabled
- [ ] Passwords are NOT logged
- [ ] No DMA leaks: `scanloop 5` shows stable "largest"
- [ ] No stack overflow: `net_task` stack watermark is > 1KB
- [ ] Up to 3 networks can be saved
- [ ] Credentials persist across reboot
- [ ] OTA updates work when WiFi connected
- [ ] Time sync works when WiFi connected

---

## Future Enhancements

1. **WPS**: One-button setup via WiFi Protected Setup
2. **5 GHz**: Support dual-band networks
3. **Captive Portal**: Handle login walls (coffee shops, hotels)
4. **Band Selection**: Choose 2.4 GHz vs 5 GHz
5. **Roaming**: Auto-switch between saved networks with same SSID
6. **Metrics**: Signal strength graph over time
