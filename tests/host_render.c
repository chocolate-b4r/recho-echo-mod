/* Host-side render test for patches/charge_screen.c: draw() into a fake 320x170 frame buffer. */
#include <stdio.h>
#include <stdlib.h>
typedef unsigned int u32;
static unsigned short fb[170][320];
static int calls_outside;
static void fill_rect_impl(int x0, int y0, int x1, int y1, u32 c) {
    if (x0 < 0 || y0 < 0 || x1 > 319 || y1 > 169 || x0 > x1 || y0 > y1) { calls_outside++; return; }
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) fb[y][x] = (unsigned short)c;
}
static void nop(void) {}
#define fill_rect fill_rect_impl
#define screen_all nop
#define lcd_flush nop
#define HOST_TEST
#define BRIGHTNESS 5
static volatile u32 fake_adc[3];
#define ADC fake_adc
static u32 ms_now(void){return 0;} static void delay_ms(u32 x){(void)x;}
static u32 cable_present(void){return 1;} static u32 power_key_down(void){return 0;}
static u32 gpio_read(u32 a,u32 b){(void)a;(void)b;return 0;} static u32 last_battery_mv(void){return 3900;}
static int mv_to_permille(u32 mv){return (int)mv;} static void backlight(int l){(void)l;}
static void power_down(void){exit(0);} static void reboot(void){exit(0);}
#include "../patches/charge_screen.c"
int main(int argc, char **argv) {
    int pm = atoi(argv[1]), full = atoi(argv[2]);
    draw(pm, full);
    FILE *f = fopen(argv[3], "wb");
    fprintf(f, "P6 320 170 255\n");
    for (int y = 0; y < 170; y++) for (int x = 0; x < 320; x++) {
        unsigned short c = fb[y][x];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("permille %d full %d: out-of-bounds fills %d\n", pm, full, calls_outside);
    return calls_outside != 0;
}
