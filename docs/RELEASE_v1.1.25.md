# v1.1.25 - Bounded latest-download recovery

**Known verification gap:** the real device successfully fetches GitHub's latest
descriptor and starts/resumes the correct application, but repeated transfers
stall before completion on the tested connection. A complete OTA installation
and reboot is not certified. Use USB for installation. No failed transfer
selected a new boot slot or altered the stored pet.

Includes [v1.1.24](RELEASE_v1.1.24.md): save-preserving web updates, GitHub latest
OTA, four/six-tile menus and the original spider with one small beetle on the
website. Spider artwork and all337 PNG assets remain unchanged.

The first hosted-device tests exposed HTTPS streams that stalled partway through
the application. Retry descriptor `ESP_ERR_HTTP_EAGAIN` reads within30 seconds.
For a stalled application stream, reconnect using an exact verified HTTP206
Content-Range at the received offset, retaining the running SHA-256 and flash
position. Limit recovery to16 reconnects and a five-minute application deadline.
Wrong offsets, lengths and totals are rejected, and the full hash is still
required before boot selection. Log actual progress and
received/expected byte counts; preserve the real transport error on failure.
Closed, truncated, oversized or hash-mismatched transfers still cannot select
the inactive slot. Existing pet storage and credentials are not rewritten.

The console wait now covers association, redirects, download and teardown.
Failed v1.1.24 attempts safely aborted and preserved the exact stored pet bytes;
they were not accepted as successful OTA evidence or hidden by replacing that
release's immutable firmware assets.

USB updates check the installed layout and compare every NVS byte afterward.
They are not atomic app/art transactions: back up first. Browser transport tests
are mocked; physical browser flashing still requires a granted serial port and
is not claimed. Artwork changes require USB. No provisioned signed firmware or
functional rollback is claimed.

Final measured verification and hosted-download results are recorded in
[PROGRESS](../PROGRESS.md).

Verified: browser164/164; packaging/art/capture44/44; hardware48/48;
build/full flash exit0 with zero warnings; native manifest/range and grid/slider
diagnostics PASS. render500/scanloop5 drift0; internal/DMA largest63,488.
Original pet restored;40-second clean-boot soak PASS, zero faults/errors.
