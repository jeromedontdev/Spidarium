# WiFi implementation reference

Current source and lifecycle are documented in
[WiFi/RTC/OTA architecture](docs/WIFI_ARCHITECTURE.md).
Verification belongs in [handoff](docs/AGENT_HANDOFF.md).

The original temporary WiFi infrastructure handoff is superseded. Static BSS
is not automatically PSRAM; pausing does not defragment memory; calling
`lv_timer_pause` from a worker is unsafe. There is no accepted “first scan fails”
behavior. The worker uses the display adapter's acknowledged pause API.
