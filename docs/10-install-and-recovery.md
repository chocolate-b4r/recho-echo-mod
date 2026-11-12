# 10 · Installing and recovering

## Before any flash

1. Charge above 50 %.
2. Copy everything you want to keep off the internal drive.
3. Remove the SD card.
4. Run the verifier on the exact file you will copy. It must say `all checks passed`.
5. Have the recovery image for your player ready: FiiO's own firmware, from FiiO.

## Echo Mini

Copy `HIFIEC90.IMG` to the top level of the internal drive, delete any other `HIFIECnn.IMG`, eject,
unplug, restart. RECHO's updater writes the backup slot, verifies it, then the primary. To undo,
copy your original RECHO image the same way.

## Echo (full size)

- **First time (from FiiO stock or Podium):** copy the patched image as `ECHOV4nn.IMG`. FiiO's
  updater only accepts `ECHOECHO.IMG` / `ECHOV100`–`ECHOV498`, and the highest number wins. Remove
  any other `ECHOV*.IMG` first.
- **Later updates:** copy the image as **`RECHOECH.IMG`** (or `RECHOEnn.IMG`). RECHO's updater
  installs it at the next restart and deletes the file. Never rename a Mini image to an Echo name or
  the other way round.

## Recovery through maskrom (last resort)

Maskrom is the SoC's boot ROM USB loader. It lives in ROM, so a bad write can't break it.

**Enter maskrom (cold, the reliable way):**

1. Unplug USB.
2. Press **RST** with a paperclip.
3. Hold every key except Power.
4. Plug USB in and let go after 2–3 s. The screen stays black.

On the Echo build, holding ≡ for 10 s also works, but writes from that state failed once in
testing, so prefer the cold entry.

**Windows driver:** the first time, open Zadig, choose Options → List All Devices, pick USB ID
**2207 262D**, and install **WinUSB**.

**Tool:** rkdeveloptool 1.32 with the Echo patch (5000 ms timeout, 32-sector transfers, maskrom id
`2207:262D`; Podium's `tools/patch_rkdeveloptool.py` applies it).

```bash
rkdeveloptool ld            # must list "Maskrom"
rkdeveloptool td            # must print "Test Device OK."
rkdeveloptool wl 0 FiiO-stock.IMG
rkdeveloptool rl 0 <sectors> readback.bin   # optional: compare with the image
rkdeveloptool rd            # "Reset Device OK."
```

| Player | Recovery image | Size |
|---|---|---|
| Echo | FiiO `ECHOV200.IMG`, SHA-256 `c6a3b6c8…8d57` | 39,845,892 B (77,825 sectors to read back) |
| Echo Mini | FiiO `HIFIEC40.IMG` | — |

Rules:
- Never run `db`, `ul`, `ef` or `wlx`.
- If `ld` / `td` misbehave, unplug, press RST and start over.
- If `wl` fails ("Write LBA failed!"), the device is still recoverable: enter maskrom cold again and
  repeat.

FiiO's firmware then starts, and you install the patched image by file copy again.
