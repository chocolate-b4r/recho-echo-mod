# 09 · The verifier

```bash
python tools/verify_image.py ORIGINAL.IMG PATCHED.IMG          # Echo Mini
python tools/verify_image.py ORIGINAL.IMG PATCHED.IMG --echo   # full Echo
```

It compares the patched image with the original **byte by byte**. It prints one line per check
and exits non-zero with "do not flash" if any check fails. It also fails on the unpatched original
(negative test). Current counts: 77 checks for the Mini, 99 for the Echo.

| Section | What it checks | Why |
|---|---|---|
| [1] container | size; outer header, RKnanoFW header and 91 load rows identical; header self-copy; CRC trailer; code row below the loader base; both `.data` images unchanged except the chooser count | ReChord's brick causes |
| [2] changed bytes | every differing byte lies in a named region (hook site, cave, a module, a patch site, icon pack, trailer); prints how many bytes changed in each | no stray edits |
| [3] code cave | zero in the original; patch code sits inside with margin; nothing in fw1 or the modules points into it (decoded literal loads) | the cave is really unused |
| [4] hook | cave bytes == `build/hooks*.bin`; every call target is an allowed RECHO routine; every data address is an allowed variable; every hardware register is expected; no text, logging or storage in the charging screen and power paths; tag and queue reads only where intended | the hook does only what it says |
| [5]–[7] modules, menu, icons | every module CRC valid and headers unchanged; menu bound / count / dispatch / USB and Power rows; icon pack passes the firmware loader's rules | module loader, home screen, icons |
| [8] chooser | rows, opener, select, countdown power-off | cable behaviour |
| [9] Settings | row count; each row selects and shows its original setting; row-specific checks follow their rows | no mismatched settings |
| [13] buttons | back-key tap, labels, hold-menu call; m45 changed only at known sites; main menu key pointer → `menu_key`; m44's folder-tree routine is the one `play_all_tick` checks for; USB-disk key call → `usb_key_exit` | key remap |
| safety | `about_tick` clears RECHO's sticky VOPF display fallback; boot splash → `boot_screen`; queue reads only from Up Next and the lyrics loader (Mini: only `mini_upnext_tick`) | display and storage |
| [10] Echo port | panel init calls; window offset; GPIO set trampoline; jack reads; repair off; updater names; panel table == Podium's (if `PODIUM_PANEL` is set) | Echo hardware |
| [12] Echo layout | content bottom 169; HINTS → stub; m45 draw → `np_draw`; chooser → `usb_draw` | Echo screens |
| [11] Echo extras | buffers inside free RAM; free RAM unreferenced (decoded literals); USB-disk entry closes the queue first; tick wrapper calls all Echo ticks; charging screen blanks and centres; panel init clears memory | Up Next safety |

## Allow-lists

At the top of the file: `ALLOWED_CALLS`, `ALLOWED_DATA`, `ECHO_CALLS`, `ECHO_DATA`, `ECHO_REGS`,
`ECHO_CONSTS` (literals that look like addresses but are packed bytes, e.g. a window command).
When you call a new RECHO routine, add it there **with a comment saying what it is**. The verifier
fails until you do. That is the point.
