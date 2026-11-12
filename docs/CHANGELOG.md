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
| v29 | display fallback no longer sticks: after 5 s on the slow GPIO panel path the "VOPF" token at 0x0309FFB0 is cleared, so the next start tries VOP again | built, verified |
| v28 | boot text in RECHO's regular small font (boot font only as a fallback); tab arrows centred, "sys" 2 px lower | built, verified |
| v27 | terminal-style boot screen (`boot_screen` replaces the `RECHO 0.10.1` splash); owner's "sys" and Bluetooth tab icons; menu tips removed; play-mode glyph in the menu's top bar | built, verified |
| v26 | menu tabs use the owner's 16 px folder, disc and wrench; Power off and About icons removed from the pack | built, verified |
| v25 | menu tabs: pixel folder, disc and wrench icons; Now Playing: big play / pause replaced by one small "Next  Artist - Title" line, progress bar on the menu's row (158) | built, verified |
| v24 | menu to the owner's drawing (one row of tabs: folder, sys, disc, Bluetooth rune, wrench; selected between < >; progress bar under the tabs), opens on Now playing; top bar "21:47 \| Fri 13 Nov", battery % in white; Now Playing: output in the top bar, queue position in the format line; volume shown from 0 dB (RECHO's -90 dB) upward | built, verified |
| v23 | queue number = position in the play order (shuffle-aware), not the file order | built, verified |
| v22 | earbud pause / play work again (no longer tied to Bluetooth streaming) | built, verified |
| v21 | smaller 12 x 12 menu icons (2x), narrower play icon | built, verified |
| v20 | main menu keys work again (menu_draw sets the row count RECHO's own draw used to set); tip line and tab labels in the large font | built, verified |
| v19 | **new main menu** (pixel-art tabs: Files, USB disk, Now playing, Bluetooth, Settings; big title, live details); About moved to the bottom of Settings (RECHO's About panel); Spectrum and Debug modules blanked | built, verified |
| v18 | main menu: Files, Now playing, **Bluetooth**, Settings, USB disk, About (Bluetooth uses Supporters' slot); Settings without Equalizer and the Bluetooth row | built, verified |
| v17 | menu bar: battery as a percentage (battery icons gone), no headphone icon; main menu without Power off (the Power key does it) | built, verified |
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
| 32 | as Mini v29 (Up Next and the strip need VOP; they vanished while RECHO stuck to the GPIO fallback after quick power-offs) | built, verified |
| 31 | Now Playing strip: the progress bar right under the picture (no empty row); boot text and tab row as Mini v28 | built, verified |
| 30 | boot screen as Mini v27; Now Playing section moved down a little, file info in the large font when it fits, no lyric in the strip on Now Playing; menu as Mini v27 | built, verified |
| 29 | Now Playing: "Artist - Song" and the lyric line at 2x (large font doubled, when they fit), album, file info, rule, lyric; next lyric dimmed in the strip; no play / pause glyph (paused = dimmer). Menu: owner's icons. Icons removed: Power off, About, header play / pause. Bluetooth: routed for the whole connection, as FiiO does | built, verified |
| 28 | menu: large text on both lines, no rule above the tabs, pixel folder / disc / wrench icons | built, verified |
| 27 | menu to the owner's drawing (tabs in one row, larger grey tip), opens on Now playing; volume shown from 0 dB upward; top bar with a divider | built, verified |
| 26 | Bluetooth: one-off GRF / CRU / GPIO dump while streaming (module answers `CYOK`; volume makes no difference) | device |
| 25 | Bluetooth diagnostic: raw module reply to each `CY0` logged | built, verified |
| 24 | queue number = position in the play order; Bluetooth routing fixed to Podium's (no test variants) | built, verified |
| 23 | Bluetooth audio test round 2 (GPIO2.1 low, GPIO2.2 low variants); earbud pause / play not tied to streaming | built, verified |
| 22 | Bluetooth audio test round 1 (variants, register log); quicker earbud play / pause | device: silent in all 4 |
| 21 | **Bluetooth audio (first try)**: streams while connected, playing and no jack in use (DAC mute + wired output off, stock settings once, CY0 0x1E); headphone buttons skip / pause; next Up Next song brighter; menu icons as Mini v21 | built, verified |
| 20 | as Mini v20 | built, verified |
| 19 | as Mini v19 | built, verified |
| 18 | as Mini v18, plus Up Next: next song light grey, later lines darker, both a step darker while paused | built, verified |
| 17 | menu bar: no play time (the strip has it), no headphone icon; queue position after the format line ("3/500"); later Up Next lines dimmer, all one step dimmer while paused; main menu without Power off | built, verified |
| 16 | top bar shows "BT <headphones>" in green while Bluetooth is connected; lossless files show their average bitrate (file size / length) | built, verified |
| 15 | Now Playing draft C: output (3.5 / 4.4 mm) in the top bar, "Artist - Song", album, rule, format line; strip on Now Playing = lyric + progress; **4.4 mm sense pull on (GRF +0x54)**, so no ghost 4.4 mm; USB symbol on the cable chooser; lyrics log line | built, verified |
| 14 | **lyrics**: the strip's top line shows the current line of `Song.lrc` (next to `Song.flac`), read once per track while the screen is on | built, verified |
| 13 | **Settings → Bluetooth**: on/off, search, connect, disconnect (no audio yet); diagnostic removed | built, verified |
| 12 | Bluetooth scan 30 s (module answers on the Echo: `CV15`, `CQIOK`, `CKKK`) | built, verified |
| 11 | player strip: format line and progress on Now Playing, track and progress elsewhere ("Next" removed); jack fix; Bluetooth step 2 | built |
| 10 | RECHO at the top, Up Next full height (11 tracks), player strip under RECHO's picture (format line and next track on Now Playing, track and progress elsewhere, messages on top); Bluetooth diagnostic | built |
| 9 | unused icons blanked (incl. battery cells) | built |
| 8 | battery glyph removed from RECHO's own top bars (Up Next shows %); screens and clock shared with the Mini (`ui_np.c`, `clock.c`) | built |
