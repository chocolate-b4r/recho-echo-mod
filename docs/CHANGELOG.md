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
| v47 | Mini: the battery % in the menu bar uses the Now Playing colours (green on the cable, amber <= 35 %, red <= 15 %); it was always white (RECHO's `movw r2,#0xF79E` at `0x3059490` -> `bl battery_colour_shim`) | built, verified |
| v46 / Echo stage 54 | **≡ does two things only:** tap opens Files from any screen, hold adds / removes the favourite on Now Playing or with the screen off (elsewhere it opens Files); it no longer selects, toggles or returns to the last screen (`shell_key` in front of RECHO's SHELL key filter); `text_at` shared by Bluetooth and the UI (100 B back) | built, verified |
| v45 / Echo stage 53 | **Favourites is a row in Files' drive list**, under Internal and SD card (▶‖ plays `FAVORITE.M3U`); the *Favourites* folder is no longer needed (removed from both players) | built, verified |
| v44 | Files always opens on the drive list (Internal / SD card), cursor on Internal; off-state charging screen about 60 % size, centred (Mini) | built, verified |
| v43 | **play favourites from Files:** ▶‖ on the *Favourites* folder at the top of the drive queues `FAVORITE.M3U` with RECHO's own queue builder; USB disk left with Back only, its screen says "or press back" | built, verified |
| v42 | **favourites:** hold ≡ on Now Playing adds / removes the song, a heart after the title, saved as `FAVORITE.M3U` (a playlist phones read); Bluetooth diagnostic logging removed (raw capture, register and pin dumps, every module message; earbud buttons still logged) | built, verified |
| v41 | **Bluetooth audio on the Mini:** routing ported from the Echo (DAC mute GPIO2.0 high, GPIO2.14 low, held by a GPIO-set hook as on the Echo; RECHO's muted DACs kept at the user's volume while streaming) | built, verified |
| v40 | clock: re-read right after Settings closes and every minute (was 5 min), so a newly set time shows at once; weekday worked out from the date (the chip's weekday can disagree) | built, verified |
| v39 | leaving USB disk opens the main menu instead of Files (one byte at 0x305BC8A: module 44 -> 47) | built, verified |
| v38 | top bars: volume first, then the output ("62 dB · 3.5 mm"); battery % green on the cable, amber / red when low (as the Echo's Up Next) | built, verified |
| v37 | no volume overlay on Now Playing (the top bar shows it); Now Playing and the main menu redraw when the volume changes | built, verified |
| v36 | top bars: volume after the output ("3.5 mm · 62 dB", "BT <name> · 62 dB", "muted") | built, verified |
| v35 | hold ▶‖ on the Files tab in the main menu plays the whole internal storage (as hold ▶‖ on "Music" in Files); Now Playing: file details right under the album in the large font, wrapped at the separator dots | built, verified |
| v34 | Now Playing: format line and queue position on the left under the album (two lines, split at a separator dot); Up Next on the right down to the progress bar, 9 songs | built, verified |
| v33 | alternate Now Playing: artist, song (two lines when long) and album on the left, Up Next (7 entries, small font, next one brighter) on the right; boot screen in RECHO's large "Power off" font, carried by the hooks (RECHO draws the boot screen before it loads its fonts, so every earlier version fell back to the bold boot font) | built, verified |
| v32 | boot screen back to RECHO's small thin font in the original layout (the large font looked bold) | built, verified |
| v31 | USB disk: Back (or power) leaves, after the key is released, so the screen stays on; Now Playing without ReplayGain; boot screen in the power-off screen's large font, 9 lines; dead code and patcher constants removed | built, verified |
| v30 | safety: the patcher refuses a hooks build older than patches/ (a failed build can no longer slip in old code); verifier checks the VOPF clear is present; the clear is logged (Log to file on) | built, verified |
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
| 52 | Files opens on the drive list, as Mini v44 | built, verified |
| 51 | favourites from Files and Back-only USB, as Mini v43 | built, verified |
| 50 | favourites and the logging clean-up, as Mini v42 | built, verified |
| 49 | lyric letters at 1.5x drawn as 2 x 2 blocks, so every stroke is 2 px (was 1 or 2 px); Bluetooth code shared with the Mini | built, verified |
| 48 | **Bluetooth audio fix:** RECHO leaves both DAC chips muted (PCM volume 0xFF) with nothing in the jacks; while streaming they are now kept at the user's volume, 6.5 dB below wired (found by comparing the stage 47 dump with Podium's); dump code removed | built, verified |
| 47 | Bluetooth: stage 46's step test (silent with every group set to Podium's values) replaced by a dump 5 s into each stream: both DAC chips (RECHO's own read, 0x3055D70), I2S1, ACODEC clock mux, CRU +0x004 / +0x014 / +0x030, GPIO levels | built, verified |
| 46 | Bluetooth step test: while streaming, every 15 s one group of registers is set to Podium's streaming state (clock gates, soft resets, five pins to inputs, GRF pin functions), restored when the stream ends; stage 45's watcher removed | built, verified |
| 45 | Bluetooth test: stock's six start-up settings applied on every route (was once), checked once a second while routed, logged at 1 s / 5 s and whenever RECHO changes one back, then applied again; old routing-variant switch removed (fixed to Podium's routing) | built, verified |
| 44 | clock fixes as Mini v40 | built, verified |
| 43 | lyric 3 px lower (wrapped lyrics: first row 3 px, the second already ends on the last row); built with `icons/mod` (same icons, now in the repo) | built, verified |
| 42 | no "Nothing playing" in the player strip or Up Next; after USB disk the main menu, as Mini v39 | built, verified |
| 41 | song title over the artist 2 px higher; top-bar order as Mini v38 | built, verified |
| 40 | volume overlay removed, redraw on volume change, as Mini v37 | built, verified |
| 39 | 2x title 2 px higher again (it touched the album); top-bar volume as Mini v36 | built, verified |
| 38 | Now Playing: artist over the 2x title 3 px lower, title 2 px lower, album 2 px higher; hold ▶‖ on Files as Mini v35 | built, verified |
| 37 | Now Playing: artist to file info back up 4 px, the rule stays (8 px of room above it) | built, verified |
| 36 | Now Playing section 4 px lower; boot screen as Mini v33 | built, verified |
| 35 | strip off Now Playing: file details centred, no pause / play glyph, no line; Now Playing lyric always 1.5x, long lines wrapped onto two rows (scaled text renders through the top-bar rows, the bar is drawn last); boot as Mini v32 | built, verified |
| 34 | strip off Now Playing shows the file details (format, rate, bits, kbps, "3/500") instead of the lyric; Now Playing lyric at 2x, else 1.5x, else the large font; USB disk Back key and boot screen as Mini v31 | built, verified |
| 33 | Now Playing strip: progress bar back in the bottom row, the line moved down to sit right above it; title at 2x on one line, or artist over a 2x title when the pair is too wide; top bar says "Safe display: restart" in amber while on the slow GPIO fallback; safety as Mini v30 | built, verified |
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
