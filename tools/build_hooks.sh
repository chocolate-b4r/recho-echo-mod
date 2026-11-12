#!/usr/bin/env bash
# Build every RECHO hook into one blob linked at the code cave (0x0306E000).
#   tools/build_hooks.sh        -> build/hooks.{bin,elf,entries}       (Echo Mini)
#   tools/build_hooks.sh echo   -> build/hooks-echo.{bin,elf,entries}  (full-size Echo)
set -euo pipefail
cd "$(dirname "$0")/.."
ARM="${ARM:-/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/12.2 mpacbti-rel1/bin}"
CFLAGS=(-mcpu=cortex-m3 -mthumb -Os -ffreestanding -fno-builtin -nostdlib -fno-pic
        -ffunction-sections -fdata-sections -Wall -Wextra -Werror)
OUT=hooks
OBJS=(build/charge_screen.o build/charge_anim.o build/keys.o)
ENTRIES='charge_screen|status_level_shim|power_tick_wrap|anim_last_frame|hold_menu|str_back|str_settings'
UNDEF=(-u status_level_shim -u power_tick_wrap -u hold_menu -u str_back -u str_settings)
if [ "${1:-}" = "echo" ]; then
    OUT=hooks-echo
    CFLAGS+=(-DECHO_PORT)
    OBJS+=(build/echo_port.o build/echo_sidebar.o build/echo_band.o build/echo_np.o)
    ENTRIES="$ENTRIES|echo_panel_init|gpio_set_echo|gpio_set_orig|echo_gpio_read|echo_hatch_tick|hatch_since|echo_sidebar_tick|echo_blank_outside|echo_panel_fill|sb_dirty|echo_band_tick|ui_borrow|ui_return|hints_stub|np_draw|usb_draw|echo_push_rows|clock_strings"
    UNDEF+=(-u echo_panel_init -u gpio_set_echo -u echo_gpio_read -u hints_stub -u np_draw -u usb_draw)
fi
mkdir -p build
"$ARM/arm-none-eabi-gcc.exe" "${CFLAGS[@]}" -c patches/charge_screen.c -o build/charge_screen.o
"$ARM/arm-none-eabi-gcc.exe" "${CFLAGS[@]}" -c patches/charge_anim.c -o build/charge_anim.o
"$ARM/arm-none-eabi-gcc.exe" "${CFLAGS[@]}" -c patches/keys.c -o build/keys.o
if [ "$OUT" = "hooks-echo" ]; then
    "$ARM/arm-none-eabi-gcc.exe" "${CFLAGS[@]}" -c patches/echo_port.c -o build/echo_port.o
    "$ARM/arm-none-eabi-gcc.exe" "${CFLAGS[@]}" -c patches/echo_sidebar.c -o build/echo_sidebar.o
    "$ARM/arm-none-eabi-gcc.exe" "${CFLAGS[@]}" -c patches/echo_band.c -o build/echo_band.o
    "$ARM/arm-none-eabi-gcc.exe" "${CFLAGS[@]}" -c patches/echo_np.c -o build/echo_np.o
fi
"$ARM/arm-none-eabi-ld.exe" -e charge_screen "${UNDEF[@]}" \
    -Ttext=0x0306E000 --gc-sections -o "build/$OUT.elf" "${OBJS[@]}"
"$ARM/arm-none-eabi-objcopy.exe" -O binary -j .text -j .rodata "build/$OUT.elf" "build/$OUT.bin"
"$ARM/arm-none-eabi-nm.exe" "build/$OUT.elf" \
    | awk -v re="^($ENTRIES)\$" '$3 ~ re {print $3, $1}' > "build/$OUT.entries"
# Refuse anything that would need .data/.bss outside the blob.
if "$ARM/arm-none-eabi-objdump.exe" -h "build/$OUT.elf" | grep -E " \.(data|bss) " | awk '$3 != "00000000"' | grep -q .; then
    echo "error: hooks need .data/.bss" >&2; exit 1
fi
cat "build/$OUT.entries"
ls -l "build/$OUT.bin"
