# 06 · User interface

What the mod draws, where the numbers come from, and how it gets pixels onto the Echo's bigger panel.
For the buttons, see [controls.md](controls.md).

| Screen | Echo Mini | Echo |
|---|---|---|
| Now Playing | ![](img/now-playing-mini.png) | ![](img/now-playing-echo.png) |
| Boot | ![](img/boot.png) | same picture, top-left 320 × 170 |

## The Echo's panel

RECHO only knows a 320 × 170 frame buffer. On the Echo's 480 × 222 panel the port shows that buffer
where RECHO always drew it, and draws everything else straight into panel memory, between RECHO's
frames.

```
 panel rows 49..218    cols 0..319: RECHO's frame buffer (the "picture")
                       cols 320..479: Up Next (rows 49..270)
 panel rows 219..270   cols 0..319: player strip (two 26-row lines)
```

### Drawing outside RECHO's picture

RECHO's drawing routines only write to the frame buffer. To use them for the extra areas, the port
**borrows** a corner of it:

1. `push_idle()` (`0x306B534`) waits until no DMA band is in flight.
2. `ui_borrow(w, h)` copies the frame buffer's top-left w × h pixels (≤ 8,320 px) to a strip in
   free RAM, clips RECHO's routines to that corner and clears it.
3. Draw with RECHO's own `fill_rect` / `draw_text` (same font, same glyphs).
4. `ui_return(x, y, w, h)` sends those pixels to the panel window (x, y) and puts RECHO's pixels
   and clip rectangle back.

All of this runs in the main loop from the POWER tick wrapper, never in an interrupt and never while
RECHO is drawing. Each pass does at most one line or one cell. With the screen off (`0x03006174` = 0)
nothing is drawn; everything is marked stale and redrawn on wake. The extra areas need the VOP bus:
on RECHO's slow GPIO fallback the top bar says **Safe display: restart** in amber
([04-echo-port.md](04-echo-port.md)).

## Top bars (both players)

`21:47 | Fri 13 Nov` on the left; on the right the volume, then the output: `62 dB · 3.5 mm`
(`62 dB · BT <headphones>` in green while Bluetooth is connected, `muted` at −128). The Mini adds the
play-mode glyph in front and the battery % at the far right, coloured like the Echo's: green on the
cable, red at 15 % or less, amber at 35 % or less, white otherwise. The Echo shows the battery in
Up Next instead.

| Piece | Source |
|---|---|
| time and date | `clock.c`: the clock chip via `0x305DA2C` / `0x305DAC4`, read at boot, wake, every minute and when Settings closes; weekday from the date |
| output | GPIO2.8 jack test (inverted on the Mini), `0x3055EC0` 4.4 mm detection, Bluetooth state |
| volume | `0x030041D0`, shown as RECHO's dB + 90 (RECHO's quietest step is −90 dB) |

`about_tick` asks Now Playing and the main menu to redraw when the volume changes, so the number
follows the keys even while paused. There is no volume pop-up any more.

## Now Playing (`np_draw` in `patches/ui_np.c`)

Module m45's draw entry `0x307A1F0` is replaced by `b.w np_draw`. The rest of m45 (key handler,
labels) stays. `np_draw` draws the whole 320 × 170 frame buffer from RECHO's player data
([02-recho-map.md](02-recho-map.md)). RECHO asks for a redraw every second while playing.

### Echo Mini: two columns

| Area | Rows | Content |
|---|---|---|
| left, x 8..154 | 36.. | artist (grey), song (white, split at the most even space onto two lines when wider than 146 px), album, then the file details in the large font, wrapped at the separator dots |
| divider | x 160, 28..143 | |
| right, x 168..312 | 33, 46 … 137 | Up Next: 9 entries in the small font, the next one brighter |
| bottom | 158 | elapsed, progress bar, length; red while paused |

**Up Next on the Mini** (`mini_upnext_tick`): after the queue or the track changes and 300 ms of
quiet, one entry per main-loop pass is named with `0x3064B64` (the queue store: one 36-byte index
read) and cached in free RAM (`0x03044000`, 9 × 64 B). It runs only while Now Playing is open.
`np_draw` only draws the cache. The verifier allows queue reads on the Mini from this function only.

### Echo: picture, strip, Up Next

| Element | Rows (picture) | Source / rule |
|---|---|---|
| `Artist - Song` at 2x | 36..67 | the large font rendered once, then copied at 2x (fits in 150 px) |
| or: artist over a 2x song | 34 / 42..73 | when the pair is too wide but the song alone fits |
| or: `Artist - Song` at 1x | 52 | |
| album | 82 | tag record `0x305A8F8`, else the folder name |
| file details | 102 | `FLAC  44.1 kHz · 24 bit · 1682 kbps · 9/10`: format record, average bitrate from size and length, play-order position |
| rule | 118 | |
| lyric line | 121..168 | always 1.5x; a line wider than 200 px is split at the most even space onto two rows; only lines too long for two rows fall back to the large font |

Scaled text is rendered at 1x into the frame buffer's top rows (the top bar's area), copied down,
then cleared; the top bar is drawn last.

**Player strip** (`echo_band.c` + `footer_row`):

| Screen | Row 1 (219–244) | Row 2 (245–270) |
|---|---|---|
| Now Playing | empty | a line, then the progress bar |
| any other | file details, centred | progress bar |
| RECHO message | the message, amber | unchanged |

**Up Next column** (`echo_sidebar.c`): header *Up Next*, play-mode glyph, battery % (green while
charging, amber ≤ 35 %, red ≤ 15 %); then 11 entries. The tick walks `0x3065FE4(queue, cur, mode, 0)`
from the current entry, the same pure function the player uses (shuffle permutation included), and
names each entry with `0x3064B64`. It redraws after 0.3 s of quiet, one line per pass. Before
USB-disk mode RECHO clears queue-active and closes the store (`0x305BC30`), so nothing reads storage
while the PC owns it.

### Play-order position (`9/10`)

RECHO's queue index is the file order. With shuffle the play order is a fixed permutation per pass,
so the position is `count − songs left`, where songs left is counted by walking `0x3065FE4` to the
end of the pass. It is cached until the track, seed, pass, count or mode changes.

## Lyrics (Echo, `patches/lyrics.c`)

Put `Song.lrc` next to `Song.flac` (same name, any case). Understood: `[mm:ss.xx]`, several time tags
on one line, `[offset:±ms]`, word tags `<mm:ss.xx>` (removed); other tags are skipped. At most 200
timed lines and 7,680 bytes (free RAM at `0x03048400`).

The file is found with RECHO's own routines: `0x3064B64` (queue entry → record, volume and folder
cluster), `0x305A02C` (volume), `0x30638FC` (find a long name in a folder), `0x3053900` and
`0x3053934` (file object, read). It is read once when the track changes, only while the screen is on.
When the line being sung changes, `echo_band_tick` asks Now Playing to redraw.

## Main menu (`menu_draw` and `menu_key`)

m47's descriptor draw (`0x307AAAC`) points at `menu_draw`; its key handler (`0x307AAA4`) at
`menu_key`, which adds **hold ▶‖ on Files = play everything** and hands every other key to RECHO
([07-buttons.md](07-buttons.md)). RECHO's own draw also wrote the row count to state + 0
(`0x307A682`), which its keys use, so `menu_draw` writes it too. Rows: Files, USB disk, Now Playing,
Bluetooth (Supporters' slot), Settings; the menu opens on Now Playing. Pixel-art tab icons are 1-bit
bitmaps in `ui_np.c`; the title is RECHO's large font doubled.

Settings → About sets m47's About flag after loading the menu module and returns to Settings when
it closes (`about_tick`). Spectrum (m49) and Debug (m50) are blanked in the image.

## Boot screen (`boot_screen`)

RECHO's splash call (`bl 0x306C3E4` at `0x3050D7C`) → `boot_screen`. RECHO draws it at ~0.17 s, but
loads its fonts at ~0.20 s, so `draw_text` would fall back to the bold 6 × 11 boot font. The mod
therefore carries the 32 characters it needs, extracted from RECHO's large font (the one *Power off*
is drawn in) by `tools/make_boot_font.py` into `build/boot_font.h`, and draws them with
`fill_rect` runs.

## Cable chooser (`usb_draw`)

The `USBSEL` screen's draw pointer (`0x3060AD4`, was `0x3059EDD`) → `usb_draw`. Two halves: *Charge
only* with a countdown (`0x030399BC`) and *USB disk* with RECHO's USB glyph drawn 3x. The cursor at
`0x030041B4` picks the outlined, bright half. Key handling is RECHO's own.

## Off-charging screen

On the Echo it blanks the whole panel, then pushes the 320 × 170 picture to columns 80–399, rows
75–244: the exact centre.

## RECHO's own top bar

Files and Settings keep RECHO's top bar, with the volume text through `vol_text` (from 0 dB up). On
the Echo its battery glyph is skipped: `bl draw_text` at `0x30594A4` becomes two NOPs and
`rsb r4,r4,#304` at `0x30594A8` becomes `mov.w r4,#312`, so the volume moves to the right edge.

## Favourites (`patches/favs.c`, both players)

Hold ≡ on Now Playing or with the screen off (`shell_key` in `keys.c`) sets a request;
`fav_tick` (POWER tick) does the work in the main loop:

1. **The song's path.** `RQUEUE.IDX` has a 36-byte header, then one 36-byte entry per song: +16 is the
   offset and +20 the length of its line in `RQUEUE.M3U`, which reads `file:///internal/` plus the
   percent-encoded path. The line is read, checked and decoded to a plain UTF-8 path.
2. **The list.** `FAVORITE.M3U` in the top folder of the internal drive, read with RECHO's
   `0x30531F0(volume, "FAVORITEM3U", file)` (find by 8.3 name) and `0x3053934` (read). Adding appends
   the path (with `#EXTM3U` first in a new file); removing takes its line out.
3. **The write.** `0x3053BF4(volume, "FAVORITEM3U", buf, len)`, the routine RECHO saves `RECHO.CFG`
   with. Up to one cluster (4 KB) it overwrites the file in place; the list is kept within that, and a
   longer one is refused (*Favourites full*). The verifier checks that only `toggle` calls it.
4. **The heart.** `fav_is_current` is worked out when the track changes on Now Playing; `np_draw` draws a
   9 × 8 red heart after the title (Echo: after the 2x title; Mini: after its last line).

Messages use RECHO's bar message (`0x305A874`, 2 s): the Echo's strip, the Mini's bottom bar.

### Playing favourites from Files (`files_key`, `play_list`)

Files' drive list (Internal / SD card, shown while `0x0303AB30` = 1) gets a third row, *Favourites*.
RECHO's draw (m44 `0x307A910`) loops over drives 0 and 1 only, 20 px rows from the list panel's top
(`0x305C428()[1]`) + 22, and its up / down step (`0x307AC18`) sets the cursor straight to 0 or 1. So
the BROWSE descriptor's draw (`0x307B5B0`, RECHO's `0x307A911`) is `files_draw`, which runs RECHO's
draw and then adds the row in its style: highlight `0x24DF` with black text, else `0xF79E`, the chevron
glyph right-aligned with `0x3059CFE`. The key handler (`0x307B5A8`, RECHO's `0x307AE11`) is `files_key`
(`keys.c`): on the drive list it moves the cursor over the three rows itself (`0x3059F74` gives a key's
step, `0x305C51C(0x307B58C)` redraws) and takes ▶‖ on row 2; everything else goes to RECHO.
`play_list` (`favs.c`) then does
what Files does for a folder (`0x307A278`): `0x305B3AC` starts a queue, each line of `FAVORITE.M3U` is
walked folder by folder with `0x30638FC` (find a name in a folder, cluster at record +260) and added with
`0x3063F74(queue, 0, folder, record, "/A/B/")`, then `0x305B3F0(-1, "Favourites")` finishes it and Now
Playing opens. Lines that are not found are skipped; with none found the queue is abandoned
(`0x305B2FC`).
