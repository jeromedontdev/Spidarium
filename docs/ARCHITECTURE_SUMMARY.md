# Architecture quick reference

- One LVGL task owns game logic/render timers and the object tree.
- CPU-only art, PCM, TLS, audio/console and main LVGL stacks use PSRAM.
- Network/save/SD stacks stay internal because flash operations disable caches.
- Worker tasks queue/copy data; they never call `lv_*`.
- Network worker pauses the **adapter with acknowledgement**, not just the
  game timers. The display holds its last frame until WiFi is stopped/deinitialized.
- With no credentials, boot never initializes WiFi. With credentials, boot
  syncs once before starting the LCD.
- NTP writes UTC to the PCF85063A RTC; RTC restores UTC at boot. Backup power
  is required for timekeeping while the board is off.
- Game saves are copied into a queue and committed off the LVGL task.
- Manual HTTPS OTA needs a saved network and the new OTA partition table.
- A pause does not defragment memory. Static BSS and stack-permission settings
  do not automatically place task stacks in PSRAM.

Read [the full design](PSRAM_THREADING_ARCHITECTURE.md),
[WiFi/RTC/OTA lifecycle](WIFI_ARCHITECTURE.md),
[art pipeline](ART_STYLE.md), and [handoff](AGENT_HANDOFF.md).
