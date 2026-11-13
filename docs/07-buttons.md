# 07 · Buttons

## Map (both players, Now Playing)

| Key | Tap | Hold |
|---|---|---|
| Vol − / Vol + | volume | repeats |
| ▶‖ | play / pause | next track |
| ≡ | Files | **Settings** (main menu on every other screen) |
| ↩ | **Back** to the main menu | previous track |

Play mode moved to Settings → Play mode. Lists (Files, menu, Settings) keep RECHO's own keys:
volume keys move, ▶‖ selects, ↩ goes back. The Echo has no on-screen hint bar; the Echo Mini keeps
its bar, which now reads **BACK / hold prev** and **FILES / hold settings**.

## How keys reach screens

A key event is `{key id, event}` (ids 1 Power, 2 Vol+, 3 Vol−, 4 ▶‖, 5 ≡, 6 ↩; events 1 press,
2 tap, 3 hold, 4 repeat). The current screen's handler (descriptor +28) gets it first. RECHO's
global filter (`0x305BE7A`…) handles power, volume, play/pause and the holds; `0x305BF18` is
"hold ≡ → main menu".

Now Playing's handler (m45 `0x307A0C0`):

| Event | Original | Now |
|---|---|---|
| ▶‖ tap | play / pause | same |
| ▶‖ hold | `0x305AB80(+1)` next | same |
| ≡ tap | `set_module(44)` Files | same |
| ↩ tap | play mode + 1, toast | `go_main_menu()` |
| ↩ hold | `0x305AB80(−1)` previous | same |

## The patch (`patches/keys.c`)

```
m45 0x307A12C   174d e879 0130   ->  bl 0x305BA3C ; b.n 0x307A170
m45 0x307A1E8   0x0307AC11 "MODE" ->  str_back     "BACK"
m45 0x307A1D8   0x0307AC07 "menu" ->  str_settings "settings"
fw1 0x305BF18   bl 0x305BA3C      ->  bl hold_menu
```

```c
void hold_menu(void)
{
    if (CURRENT_MODULE == 45)      /* 0x030399C8: Now Playing loaded */
        set_module(48);            /* 0x305B234: Settings */
    else
        go_main_menu();            /* 0x305BA3C */
}
```

To make your own remap, change the m45 handler's cases (it is a straight `cmp`/branch chain at
`0x307A0C0`–`0x307A184`) or the global filter, and add a matching check to
`tools/verify_image.py` section [13].

## Playing more than one folder (RECHO's own feature)

In Files, **hold ▶‖** on a folder queues that folder and every folder inside it (RECHO logs
`ui: queued '<name>' tree: N files`). Hold ▶‖ on **Internal** (or the SD card) at the top level
of Files queues every song on that drive, so playback runs on from one folder to the next. A
tap of ▶‖ on a song still queues just its folder. Play mode (shuffle, repeat) applies to either
queue. In m44 this is the hold-▶‖ branch at `0x307AF2C` → `0x307A278` (folder tree, `0` = the
whole drive).

## USB disk: Back leaves

RECHO's USB-disk loop only polls one callback (`0x30505EC`), and its key poll does not run there.
Once armed, it leaves on `power_key_down()` at `0x305062C`; that call is now `bl usb_key_exit`
(`patches/keys.c`). The hook also reads the key ladder itself (SAR ADC channel 1, RECHO's own ↩ band
450–549, two readings 20 ms apart; the conversion is started on one call and read on the next, so
transfers never wait for the ADC). Whichever key leaves, the hook waits for its release first, so
the player sees no press afterwards. Before, the power press that ended USB mode also reached the UI,
which switched the screen off.
