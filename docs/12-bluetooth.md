# 12 · Bluetooth (in progress)

RECHO has no Bluetooth code. This mod adds it in steps, each one tested on a real player before
the next.

| Step | What | Status |
|---|---|---|
| 1 | Diagnostic: power the module, open its serial line, log what it says | **works on both**: Mini `CV13`, Echo `CV15`, both answer `CQIOK` |
| 2 | Diagnostic: scan, connect to the first device found, disconnect | **scan command accepted on both** (`CKKK`), no device found in 15 s; now 30 s (Mini v13, Echo stage 12) |
| 2b | Bluetooth row in Settings, device list, remembered headphones | planned, depends on step 2's log |
| 3 | Route audio to the module and back to the jacks | planned |

## Results so far

- 2026-11-12, step 1: both modules start and answer every status query (`CQIOK` = idle, not connected).
- 2026-11-12, step 2: both answer `CS00` with `CKKK`, the same acknowledgement Podium's tests expect
  during a scan, and `CP00` with `CKKK` + `CP`. No `CS nn` device frame arrived in 15 s. The scan now
  lasts 30 s, as long as Podium's, and the headphones must be in pairing mode (not just switched on).

## Step 1: the diagnostic (`patches/bt_diag.c`)

**To run it:**

1. Turn Settings → Log to file on.
2. Restart the player and leave it on for 40 s.
3. Copy `ECHOLOG.TXT` from the internal drive.

The diagnostic only runs while logging is on. It starts 10 s after boot, keeps the module powered
for 25 s, then switches it off again. It doesn't touch audio and writes nothing except log lines.

**Lines to look for:**

```
bt diag: power on GPIO2.7, UART1 115200 on GPIO2.16/2.17, listening 25 s
bt diag: rx "CV15" [43 56 31 35 ]          <- the module is alive and says its version
bt diag: rx "CQ.." [...]                    <- answers to the status queries
bt diag: done, N bytes from the module, M status queries, version char 53 (0 = none)
```

- If `N` is 0 on a player, the module there either isn't powered from GPIO2.7 or doesn't talk
  on UART1 group A.
- The pins are Podium's, verified only on the full-size Echo. The Echo Mini may use different
  ones; its log decides how step 2 is wired.

## Hardware facts used

| | |
|---|---|
| Module power | GPIO2.7, high = on (RECHO's `gpio_set` `0x3051590`) |
| Serial | UART1 `0x400B0000`, function 1 on GPIO2.16 / GPIO2.17 (`0x3051570`), group A via GRF `+0xBC` = `0x02000000` |
| Clock | CRU `+0x38`: `0x00C00040` (crystal) then `0x003F0000` (÷1); divisor 13 → 115,200 8N1 |
| Reset / gates | CRU `+0xC0` bit 8 (reset), `+0x84` bit 13 (UART1 clock), `+0xA0` bit 9 (bus clock); write-masked |
| Frames | ASCII `C` + letter + 2 characters, NUL-terminated. Module: `CV1x` when ready. Player: `CQ00` status query |
| Log on/off | settings block `0x0303EDD0` byte 5 |
