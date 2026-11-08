# Current handoff and remaining checks

Use [the agent handoff](docs/AGENT_HANDOFF.md) for measured results and outstanding
validation, and [the phase plan](IMPLEMENTATION_PLAN.md) for acceptance gates.
First-release implementation and measured results are recorded in
[release verification](docs/RELEASE_VERIFICATION.md). Read its physical-browser
and battery limits before extending the website, firmware or power policy.

The earlier version of this file incorrectly said the first scan was expected
to fail and that WiFi calls belonged on the LVGL task. Neither is the design:
radio calls run on a flash-safe internal-stack worker, which pauses the display adapter with
acknowledgement and always stops/deinitializes WiFi before resuming it.

Do not reimplement obsolete always-on toggles or automatic reconnects.
Provision an authorized network using the masked WiFi editor, then verify
association, RTC sync, the next boot sync, physical battery-backed time retention,
and manual HTTPS OTA using a real uniquely versioned Spidarium binary.
