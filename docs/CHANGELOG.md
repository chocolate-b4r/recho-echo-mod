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
| v16 | rebuilt with the shared top-bar change (no visible change on the Mini) | built, verified |
| v15 | cable chooser: RECHO's USB symbol (U+E013, 3x) for USB disk | built, verified |
| v14 | **Settings → Bluetooth**: on/off, search, connect, disconnect (no audio yet); diagnostic removed | built, verified |
| v13 | Swap up/down hidden in Settings (as on the Echo); Bluetooth scan 30 s | built, verified |
| v12 | jack shown from RECHO's own detection, hidden when nothing is plugged in; Bluetooth step 2 (scan, connect) | built, verified |
| v11 | Now Playing spread over the full 170 rows; Bluetooth diagnostic (Log to file on) | built, verified |
| v10 | unused icons blanked; play-mode glyph in the Now Playing top bar | built, verified |
| v9 | no button bar (screens use all 170 rows; RECHO's messages still pop up at the bottom); own Now Playing screen with clock, queue position and battery %; two-halves cable chooser | built, verified |

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
| 16 | top bar shows "BT <headphones>" in green while Bluetooth is connected; lossless files show their average bitrate (file size / length) | built, verified |
| 15 | Now Playing draft C: output (3.5 / 4.4 mm) in the top bar, "Artist - Song", album, rule, format line; strip on Now Playing = lyric + progress; **4.4 mm sense pull on (GRF +0x54)**, so no ghost 4.4 mm; USB symbol on the cable chooser; lyrics log line | built, verified |
| 14 | **lyrics**: the strip's top line shows the current line of `Song.lrc` (next to `Song.flac`), read once per track while the screen is on | built, verified |
| 13 | **Settings → Bluetooth**: on/off, search, connect, disconnect (no audio yet); diagnostic removed | built, verified |
| 12 | Bluetooth scan 30 s (module answers on the Echo: `CV15`, `CQIOK`, `CKKK`) | built, verified |
| 11 | player strip: format line and progress on Now Playing, track and progress elsewhere ("Next" removed); jack fix; Bluetooth step 2 | built |
| 10 | RECHO at the top, Up Next full height (11 tracks), player strip under RECHO's picture (format line and next track on Now Playing, track and progress elsewhere, messages on top); Bluetooth diagnostic | built |
| 9 | unused icons blanked (incl. battery cells) | built |
| 8 | battery glyph removed from RECHO's own top bars (Up Next shows %); screens and clock shared with the Mini (`ui_np.c`, `clock.c`) | built |
