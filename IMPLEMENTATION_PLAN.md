# Spidarium overhaul: implementation phases

The requested scope is menus, on-demand WiFi for RTC/OTA, expanded gifts,
replacement non-spider art, and accurate Markdown handoff.
The later user requirement supersedes the original always-on WiFi toggle and
reconnect proposal: sync once at boot, then shut down; manual operations only.

| Phase | Deliverable | Current implementation |
|---|---|---|
| 1 | Architecture and style specifications | Reconciled against source; unsupported historical claims removed |
| 2 | Menus, networking, gifts infrastructure | Three-card paged menus, masked input, three saved networks, worker handshakes, 18 earned habitat gifts |
| 3 | Replacement artwork and pipeline | 134 deterministic PNGs: 95 non-spider assets, 39 preserved spider setup frames; native canvas anatomy, generator and validator |
| 4 | Runtime integration and verification | Prey/HUD/decor/gifts wired to assets; build/flash/hardware gates recorded in handoff |
| 5 | Documentation and agent handoff | Current APIs, radio/RTC/OTA lifecycle, art maintenance, commands, evidence and outstanding external tests |

The first-release work also includes a website, genuine USB flasher and local
save/WiFi tools, approximate browser simulator and actual screenshots.
See [v1.0.0](docs/RELEASE_v1.0.0.md) and
[measured verification](docs/RELEASE_VERIFICATION.md).

## Acceptance gates

1. Build and full flash wrappers propagate real exit codes; no main warnings.
2. First scan works without credentials; five scans return the radio's memory
   without cumulative drift beyond 4 KB.
3. Rendering, repeated menu opens, editors, and gift computation pass.
4. Internal largest block remains at least the historical 51,200-byte baseline.
5. Forty-second boot soak reports no faults. Do not equate build success with
   hardware success.
6. Actual association, saved-network boot NTP, RTC battery retention, and OTA
   require authorized credentials, backup power, and a hosted release binary.
   These are separate gates, not proven by scanning or input rejection tests.

See [handoff](docs/AGENT_HANDOFF.md) for exact observed results and remaining
external prerequisites. The old blanket checked success list was not evidence.
