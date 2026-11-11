# On-demand WiFi, RTC, and OTA

## User flow

Open Menu -> Settings -> WiFi. Merely opening the page does not enable WiFi.

- **Scan networks:** performs one scan and lists up to twelve networks, with
  dBm signal strength and open/password status.
- **Select network:** secured networks open a masked keyboard editor; open
  networks connect without a password.
- **Add network:** SSID editor followed by password editor, including hidden
  networks. Blank password means open. WPA passphrases must be 8-63 bytes.
- **Saved networks:** up to three; each can sync RTC or be forgotten.
- **Firmware update:** choose GitHub Latest Release, or enter an advanced
  HTTPS application URL. A saved network is required. There is no always-on
  connection or automatic update check at boot.

During network work the display holds its last frame and input pauses. After
stop/deinit, the display resumes and shows the result or error. Failed
association does not save credentials. A successful association saves them
even if NTP later fails, allowing retries without retyping.

The first saved network is used for requested OTA. Forgetting it promotes the
next saved network. All saved networks are available for manual sync. There is
no boot association or automatic SNTP: RTC supplies time until a sync is requested.

Boot performs only a short init/stop/deinit job before the LCD exists. This
establishes persistent netif/event allocations before internal DMA RAM fragments,
without connecting to an AP. It preserves the proven memory-priming layout while
avoiding unreachable saved networks causing boot errors or join delays.

## Radio lifecycle

`net.c` has one internal-stack worker (flash-safe NVS/OTA) and a single
outstanding copied request; eligible buffers and TLS allocations use PSRAM.
The worker pauses the LVGL **adapter** with acknowledgement before WiFi init,
then performs the operation, stops/deinitializes the driver, and resumes.
There are no background retries, modem-sleep connections, or worker `lv_*`
calls. Event handlers only publish connection/IP flags, not UI mutations.
Station netif and handler registrations are created once and reused.

Bounds: display pause 3 seconds; association/IP 15 seconds; fresh NTP callback
10 seconds; OTA download 120 seconds with individual HTTP timeout 10 seconds.
Scans use ESP-IDF's blocking scan only on the worker. Failure text is logged
and published; a generation counter tells the UI to rebuild.

## Credentials and persistence

NVS namespace `spidarium_net`, blob key `networks`:

```c
struct {
    uint32_t version; // 1
    uint32_t count;   // 0..3
    struct { char ssid[33]; char pass[65]; } networks[3];
};
```

The worker validates blob size, count, version, and string termination.
If absent, it reads original `ssid`/`pass` keys for migration. Credentials
are separate from the game save. Only the worker writes/commits credentials;
passwords are not logged or returned by the console.

**NVS is not encrypted in this build.** Physical flash access can recover
credentials. Do not describe this as encrypted storage. Flash/NVS encryption
requires a separate device-provisioning decision.

## RTC timekeeping

PCF85063A uses address `0x51` on the existing BSP I2C bus:
SCL GPIO14, SDA GPIO15. No second bus or interrupt driver is installed.

- Boot reads calendar registers `0x04..0x0a`, validates BCD/calendar fields
  and oscillator-stop flag, then restores system UTC.
- Invalid/unpowered RTC produces a warning, not a fake time.
- No saved credentials: skip WiFi initialization entirely.
- Saved credentials: one boot association and fresh NTP sync **before starting
  the display**, then stop/deinit.
- SNTP notification, not a pre-existing system time, proves a fresh sync.
- Write UTC in 24-hour mode with STOP set during calendar writes and cleared
  afterwards. Supported years are 2000-2099.

Register definitions follow the
[NXP PCF85063A datasheet](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf).
Writes are read back and checked within one second; stopped/12-hour-mode
calendars are rejected during boot rather than misinterpreted.

Time while powered off depends on RTC backup power. A reset test does not prove
battery-backed retention during a physical power-off.

## OTA implementation and deployment

The partition table now contains a 3 MB factory image, two 3 MB OTA slots,
OTA metadata, and a 6 MB asset partition. NVS remains at its original address.
**Initial migration requires a full `flash.ps1 COM7`**, not an app-only write:
bootloader, table, application and repacked assets must agree.
Back up user data before deployment. No whole-flash erase is required.

HTTPS uses the ESP-IDF certificate bundle and certificate/hostname verification.
GitHub release downloads require redirects: the latest-release path follows at
most five redirects and rejects non-HTTPS destinations. The image must have the same project name;
an uninitialized system clock triggers a fresh RTC/NTP sync before TLS.
the running version is treated as up to date. Different-version firmware is
downloaded to an inactive OTA slot, completeness/image verification is checked,
and only successful finish changes the next boot partition. Reboot explicitly
to apply (console `reboot`); download alone does not reset the device.

**Update From GitHub** resolves
`https://github.com/jeromedontdev/Spidarium/releases/latest/download/spidarium-ota.json`.
This is GitHub's latest stable release, not a hardcoded version or Pages cache.
The bounded 4 KB manifest supplies a numeric version, exact versioned
`spidarium.bin` URL, application size/SHA-256 and required storage size/SHA-256.
Identical versions report up to date; older versions are rejected.
The worker checks the installed artwork bytes before writing. Different artwork
requires the USB updater rather than risking an incompatible application.
It streams the application through one 4 KB PSRAM buffer into an inactive slot,
checks the exact received length and SHA-256, ends ESP image verification and
checks the project/version descriptor before changing the boot partition.
NVS and artwork are never written by this flow. Press RESET or use console
`reboot` after the installed result; queue acceptance is not installation.

Release packaging writes `spidarium-ota.json` alongside the five images.
Publish **both that descriptor and the standalone `spidarium.bin`** as GitHub
release assets, in addition to the complete ZIP. Their versioned URL prevents
a moving latest alias from mixing releases midway through a download.
Future artwork changes must ship the complete USB update bundle.

The advanced URL path remains an explicit direct-binary operation, without
the latest-release manifest's hash/artwork compatibility/downgrade checks.
Only use an application built for the installed art pack.
Application-level functional rollback and signed-release provisioning are
not enabled. TLS authentication is not equivalent to firmware signing.
OTA updates the application only, not asset storage. Changes to packed artwork
or generated asset identifiers require a matching full flash; do not deploy
incompatible app/asset combinations through application-only OTA. The web
updater writes matching application/artwork and initializes OTA selection last,
but preserves and readback-verifies NVS. A mismatched installed partition table
is rejected before writes; back up the pet and use full installation to migrate.

## Verification

See [serial console](serial_console.md) for commands and
[handoff](AGENT_HANDOFF.md) for observed results. A successful scan does not
prove association, RTC sync, or OTA. Those require actual credentials and
an accessible HTTPS firmware endpoint; report missing evidence explicitly.
