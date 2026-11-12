# Changelog

Builds are patched from RECHO 0.10.1 `2d11bdcd`. "Device" means tested on a real player.

## Echo Mini

| Build | Changes | Status |
|---|---|---|
| v1–v2 | Spectrum + Supporters removed; off-state charging screen; 19 × 7 battery icons | v2's charging screen restarted the player (it drew text) |
| v3 | charging screen redrawn with rectangles only, ADC read directly | device: power-off works |
| v4–v5 | charging animation; your icon set; bolt removed while charging | device |
| v6 | Debug removed from menu; Settings without Show tags / On USB connect / Debug strip; cable chooser Charge only / USB disk, no DAC | device |
| v7 | no key for 5 s on a cable → power off into the charging screen | device |
| v8 | button remap: ↩ tap = Back, hold ≡ on Now Playing = Settings; bar labels | built, verified |

## Full-size Echo

| Stage | Changes | Status |
|---|---|---|
| 1 | ST7796S panel, Echo pins, RECHO centred in the panel, updater and repair off, Menu-10 s maskrom hatch | device: works |
| 2 | Up Next column; full panel-memory clear (fixes a stray mark from boot) | device: works |
| 3 | RECHO at the top, clock / date / battery band, Swap up/down removed, **RECHO's updater on as `RECHOECH.IMG`** | device: works (the updater itself not yet exercised) |
| 4 | button bar moved to the bottom, slim info line, Now Playing text centred | built |
| 5 | own Now Playing screen (red paused state, volume on change), full-width bar, two-halves cable chooser, centred charging screen, battery and mode in Up Next | built |
| 6 | button remap | built |
| 7 | no button bar; screen and Up Next centred vertically; RECHO's bar messages on one line | built |
