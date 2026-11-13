# 07 · Buttons (internals)

> [!TIP]
> Looking for what the buttons do? See **[controls.md](controls.md)** and its
> [cheat sheet](img/controls.png). This page is about how each change is wired.

## Every key change at a glance

| Where | Key | RECHO 0.10.1 | With the mod | Wired at |
|---|---|---|---|---|
| Now Playing | <kbd>↩</kbd> tap | next play mode + toast | back to the main menu | m45 `0x307A12C` |
| Now Playing, screen off | <kbd>≡</kbd> hold | main menu / nothing | add to / remove from favourites (`FAVORITE.M3U`) | SHELL filter `0x3060B80` → `shell_key` → `favs.c` |
| every other screen | <kbd>≡</kbd> tap / hold | depends on the screen (main menu: last screen; Settings: toggles; hold: main menu) | open Files | `shell_key` |
| Main menu, Files tab | <kbd>▶‖</kbd> hold | next track | play every song on the internal storage | m47 key pointer `0x307AAA4` → `menu_key` |
| USB disk | <kbd>↩</kbd> tap | nothing | leave USB disk | fw1 `0x305062C` → `usb_key_exit` |
| USB disk | <kbd>⏻</kbd> tap | leave, and the press also turns the screen off | nothing (Back leaves; the screen says "or press back") | same; text at `0x305E877` |
| Files, drive list | <kbd>Vol</kbd> up / down | two rows (Internal, SD card) | three rows, the third is *Favourites* | m44 key pointer `0x307B5A8` → `files_key`, draw `0x307B5B0` → `files_draw` |
| Files, *Favourites* row | <kbd>▶‖</kbd> tap / hold | (no such row) | play `FAVORITE.M3U` | `files_key` |
| after USB disk | (any way out) | opens Files | opens the main menu | fw1 `0x305BC8A` `movs r2,#44` → `#47` |
| Echo, anywhere | <kbd>≡</kbd> hold 10 s | nothing | maskrom | `echo_hatch_tick` ([04](04-echo-port.md)) |

Play mode moved to Settings → Play mode. Lists (Files, Settings) keep RECHO's own keys: volume keys
move, ▶‖ selects, ↩ goes back. There is no on-screen hint bar; on the Mini RECHO's bar only appears
while it has a message.

## How keys reach screens

A key event is `{key id, event}` (ids 1 Power, 2 Vol+, 3 Vol−, 4 ▶‖, 5 ≡, 6 ↩; events 1 press,
2 tap, 3 hold, 4 repeat). The current screen's handler (descriptor +28) gets it first. RECHO's
global filter (`0x305BE7A`…) handles power, volume, play/pause and the holds; `0x305BF18` is
"hold ≡ → main menu". The filters are a list RECHO fills at boot (`0x303E744`, count `0x303E740`:
HINT, SHELL, PLAY, POWER, DBG); each one's descriptor +28 is tried in turn, then the screen's.
SHELL's (`0x3060B64` +28, `0x305BD41`) is now `shell_key`, which takes every ≡ event first.

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
fw1 0x3060B80   0x0305BD41 (SHELL key filter) -> shell_key
```

```c
int shell_key(const unsigned char *ev)
{
    if (ev[0] != 5)                                   /* not ≡: RECHO's SHELL filter */
        return shell_key_orig(ev);
    int on = screen_on();                             /* 0x305CAB8 */
    if (ev[1] == 3 && (!on || CURRENT_MODULE == 45))  /* hold on Now Playing / screen off */
        fav_request = 1;
    else if ((ev[1] == 2 || ev[1] == 3) && on && CURRENT_MODULE != 44)
        set_module(44);                               /* tap (or hold elsewhere): Files */
    return 1;                                         /* press / repeat: nothing */
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

## Main menu: hold ▶‖ on Files plays everything

With the Files tab selected and the screen on, **hold ▶‖** queues every song on the internal
storage and starts playing, exactly as holding ▶‖ on **Music** at the top of Files does. The main
menu descriptor's key handler (m47 `0x307AAA4`, RECHO's `0x307A109`) is now `menu_key`
(`patches/keys.c`), which passes every other key to RECHO's handler. Hold ▶‖ on Files opens Files
(`set_module(44)`) and sets a flag. `play_all_tick` waits until m44 is loaded and has had 300 ms
to open. It then checks that the code at `0x307A278` is m44's folder-tree routine byte for byte,
and runs m44's own sequence: `0x305A02C(0)` (select the internal drive), then
`0x307A278(0, "Music")`. That routine queues the drive, starts playback and opens Now Playing.
If Files does not open within 4 s, the request is dropped. With the screen off the menu gets no
keys, so hold ▶‖ stays RECHO's "next track".
