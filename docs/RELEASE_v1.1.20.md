# Spidarium v1.1.20

- Redraw Volume as three mixer faders and Background Music as bold beamed
  musical notes. Sound keeps its speaker and Play its ball. Device and browser
  share the same deterministic pixel artwork; other artwork is unchanged.
- Fix browser slider rows: thumbnails have their own left column, the label
  and percentage sit above the track, and controls no longer overlap. This
  applies to Volume through the shared slider-row layout. Browser Brightness
  remains explicitly controlled by the player's screen, not a fake slider.
- Retain one master Sound checkbox and the independent Background Music
  preference. No audio, gameplay, save-layout or spider changes.

Regression checks cover silhouette separation (not merely distinct filenames),
generated/source pixel equality and slider layout. Real browser geometry and
input checks verify controls at multiple display sizes.
