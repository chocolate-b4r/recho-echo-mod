# 04 · Porting RECHO to the full-size Echo

RECHO targets the Echo Mini. The full-size Echo has the same SoC, the same key ladder and a very
similar board. Its differences are the display, a few pins and the eMMC layout. `--echo` applies
the patches below on top of the ones in [03-mini-patches.md](03-mini-patches.md). The code is in
`patches/echo_port.c`.

## Hardware differences

| | Echo Mini | Echo |
|---|---|---|
| Panel | 320 × 170, rows offset 35 | **ST7796S 480 × 222** landscape, MADCTL `0x28`, visible rows 49–270 |
| DAC mute / reset | GPIO2.0 / GPIO2.1 | mute GPIO2.1, no reset |
| Power pair | — | GPIO0.14 and GPIO2.0 held high |
| 4.4 mm detect | low = plugged | high = plugged |
| ADC ch2 | — | 3.5 mm headset mic (floating, shows up in the log) |
| Maskrom | USB `2207:262D` | same id, same key combination |

Unchanged: key ladder values, power key GPIO2.5, hold GPIO2.6, charger done GPIO2.3, backlight
GPIO2.4, battery on ADC ch0, CS43131-compatible DAC setup.

## Patch sites

| Site | Change |
|---|---|
| `0x306B9F4`, `0x306C206` | after sleep-out, `bl 0x306B000` (Mini panel init) → `bl echo_panel_init`: power pair high, 100 ms, the 19-command ST7796S table, clear all 480 × 320 of panel memory, mark the Echo screens for redraw |
| `0x306B20C`, `0x306B20E` | window row offset `adds r5/r4,#35` → `#75`. RECHO's 320 × 170 picture sits at columns 0–319, rows 75–244 (49 + 26: centred) |
| `0x3051590` (4 B) | GPIO set entry → `b.w gpio_set_echo`: (2,0) → (2,1), (2,1) dropped. `gpio_set_orig` replays the 4 original bytes and returns to `0x3051594` |
| `0x3055EE8`, `0x3055F6A` | GPIO2.8 / GPIO2.0 reads → `echo_gpio_read` (4.4 mm inverted; mute read from 2.1) |
| `0x3050FBE` | FW1 repair call → `movs r0,#1; nop` (repair stays off) |
| `0x305F3BF`, `0x305F3CB` | update file names `ECHOMINI.IMG` / `HIFIECnn.IMG` → `RECHOECH.IMG` / `RECHOEnn.IMG` |
| `0x306AC68` wrapper | runs the Echo ticks each main-loop pass: escape hatch, Up Next, clock / message line |

Plus the screen changes in [06-echo-ui.md](06-echo-ui.md).

## Escape hatch

Hold **≡ for 10 s** anywhere: `echo_hatch_tick` reboots into the boot ROM's USB loader (maskrom)
with Podium's sequence (CRU gates, GRF boot select, PMU flag `0xFFFF18AF`, `SYSRESETREQ`). Note: a
cold maskrom entry (RST + every key except Power + USB) was more reliable for writes than this
software entry. See [10-install-and-recovery.md](10-install-and-recovery.md).

## Why RECHO's updater is safe on the Echo

RECHO's writer takes the firmware area from the eMMC ID block, not from fixed Mini numbers. It reads
the system size at sector 64 + `0x202`. Slot 2 is at half the area, and every write stays inside it
and is read back. Boot log of a real Echo running this port:

```
emmc: 7733248 sectors
idb copy 0: P 28000 R 14000 user_base 8e000 (stock's 8e000) user_sectors 6d2000
sector 2a000: 45564153 ...           ("SAVE" block right after the 0x28000-sector area)
fw1 1 fw2 1                          (valid RECHO images found at both slot offsets)
```

- **Firmware area:** 0x28000 sectors.
- **Slot 2:** at 0x14000, which is exactly the R the boot loader reports.
- **User drive:** starts where stock firmware puts it.

The writer therefore overwrites exactly the two slots the loader boots from. The only Echo-specific
risk was the file names: a Mini image named `HIFIEC90.IMG` would have installed. That's why the Echo
build uses its own names.

## Installing on an Echo for the first time

The Echo's stock firmware updater only accepts `ECHOVnnn.IMG`. From FiiO's firmware, copy the
patched image as `ECHOV4nn.IMG`; FiiO writes both slots. Afterwards, use `RECHOECH.IMG`.

Podium's updater accepts only Podium images and FiiO's own `ECHOV200.IMG`, so from Podium the way
back is two copies: `ECHOV200.IMG` (Podium installs FiiO), then the patched image as `ECHOV4nn.IMG`
(FiiO installs it).

RECHO's own updater does not check whose firmware a file is. It refuses an image that is not
RKnanoFW, whose backup header differs, whose size or layout does not fit the firmware area, whose CRC
is wrong, or whose slot 2 would overwrite the music drive (result table at `0x305F320`). Any valid
Echo image named `RECHOECH.IMG` therefore installs, Podium's `ECHOV461.IMG` included.

## Display fallback (VOP / GPIO)

RECHO's panel start-up (`0x306C018`) writes `VOP1` to `0x0309FFB0` before trying the fast VOP bus and
clears it after 5 s on VOP (`0x306BDA8`). A start that finds `VOP1` (the last one ended within 5 s)
writes `VOPF` and uses the slow GPIO path; a start that finds `VOPF` does the same, so the fallback
sticks while RAM stays powered (on a cable "off" is the charging screen). Up Next and the player strip
push through the VOP bus and stay blank on the GPIO path. Since stage 32 `about_tick` clears `VOPF`
after 5 s on the GPIO path, so the next start tries VOP again. Without the fix: unplug, switch off
(power is cut, RAM is lost), switch on.

Since stage 33 the top bar shows **Safe display: restart** in amber whenever RECHO is on the GPIO
fallback, so the missing sidebar and strip are explained on screen; one restart brings them back.
The verifier fails any image without the `VOPF` clear in `about_tick`.
