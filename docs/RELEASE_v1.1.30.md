# v1.1.30 - USB chimes and ten original soundtracks

## Sound

- USB attachment plays a soft rising two-note chime; unplugging plays its
  descending counterpart. Both last320ms and respect master mute/zero volume.
  Two consecutive one-second PMIC readings confirm each edge. Contact bounce,
  read errors and a change to full charge do not cause repeated chimes.
  Booting connected plays once, even with a full battery. Screen lock still
  permits chimes; USB insertion does not wake true deep sleep.
- Seven new original scores join the unchanged Silk Garden, Moonlit Hollow
  and Rainy Canopy: **Crystal Echoes, Mosslight Waltz, Ember Hearth, Starlit Web,
  Bamboo Rain, Clockwork Fern and Dewdrop Lullaby**.
  Choose under **Settings > Sound > Soundtrack**. Music keeps its independent
  switch/volume; master sound still wins.
- Native/browser arrangements and integer PCM match. Each new score has its
  own chord/melody, rhythmic pattern, tempo and voice balance. Ten theme icons
  and paginated selection work on device/browser; selecting the last score
  keeps its page visible. No artwork or spider changes.

## Saves and installation

Save v14 retains the268-byte prefix, six-byte decor records and52-field schema.
The existing track byte now accepts0..9. Old v2-v13 backups remain readable;
v11-v13 still reject new track IDs. Older firmware cannot read new v14 saves.
Export a backup before updating or considering a downgrade.

Use the save-preserving USB updater or complete five-image release ZIP.
No NVS image is included. Artwork is identical to v1.1.29.
No completed hosted OTA/reboot, signed firmware, encrypted NVS, functional
rollback, physical unplug/listening or deep-sleep USB-wake claim is added.

## Verified capacity

App2,056,976 /3,145,728 bytes: **1,088,752 bytes (1.04MiB) free per app slot**.
Artwork1,662,738 /6,291,456 bytes: **4,628,718 bytes (4.41MiB) free**.
Both chimes plus seven scores add3,328 bytes versus v1.1.29, with no new PCM
assets/buffers. Three3MiB application slots are reserved, not one9MiB app slot.
The24KiB NVS partition's actual utilization is not measured.

After the final40-second soak: internal free134,339 bytes, largest63,488,
PSRAM free2,258,528 bytes. Contiguous internal/DMA capacity remains the tighter
resource; largest exceeds the51,200-byte floor.

## Verification

Warning-free build/full flash exit0. Browser191/191, host75/75 and native52/52
pass. Actual playback starts all ten tracks. Ten full-loop native hashes match
browser hashes with peak<4000, step<1000 and RMS300..1300. Both5,120-sample
chimes pass PCM checks and complete actual codec writes. Actual boot-on-USB
checks confirm exactly one audible chime, mute suppression and volume-zero
suppression. Unplug detection is covered by the PMIC edge self-test; physical
unplug/speaker listening remains human verification.

Actual compiled save schema14/52 fields and track9 survive reboot alongside
273 decor placements/all three paths. Render500/scanloop5 drift0; largest63,488.
A40-second software-reboot soak reports PASS with zero faults. The RTS-reset
soak path encountered Windows USB re-enumeration; this does not count as a
passed RTS-reset test. The fresh280-byte pet backup is restored exactly and
ownership/habitat/Silk are rechecked after reboot.

See the latest [progress round](../PROGRESS.md) for publication evidence.
