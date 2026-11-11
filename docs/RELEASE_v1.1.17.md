# Spidarium v1.1.17 - a quieter little habitat

- Replace jumpy automatic chirps and falling pitch effects with soft,
  sustained air and leaf textures. Two-second attacks, three-second releases
  and gentle swells leave space for the spider and foreground interaction.
- Pounce is a filtered noise whoosh instead of a laser-like rising tone.
  Other action effects are quieter; petting retains a short, soft chirp.
- Browser volume now changes actual output; mute also stops existing sounds
  while menus are open. Hidden tabs silence audio.
- Foreground effects fade ambience out. Native radio work also interrupts
  long textures promptly rather than waiting eight to ten seconds.

The existing audio worker and reusable PSRAM PCM buffer remain in use. No
codec writes, synthesis or radio waits are added to LVGL. Save format, spider,
artwork, menu layout and minigame rules are unchanged.

`audiotest` verifies exact seeded PCM against browser hashes. `audio_check.py`
checks full codec-write sample counts, playback duration, foreground/radio
interruption and radio teardown. See the latest [progress round](../PROGRESS.md)
for measured evidence. Waveform/write checks do not replace listening approval
on the physical speaker or a player's headphones.

Install the official five-image ZIP with the website flasher or the normal
full-flash wrapper; NVS is preserved. Existing v1.1.16 artwork is unchanged.
Personal, non-commercial license and third-party notices are unchanged.
