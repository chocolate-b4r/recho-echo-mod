/*
 * Off-state battery screen for RECHO 0.10.1 (build 2d11bdcd) — v3.
 *
 * Hooked in place of the final `bl 0x3051e28` (power rail cut) inside the
 * power-off routine at 0x305a618, so every power-off path (menu, auto-off,
 * power-key hold) passes through here with IRQs still enabled.
 *
 *  - On battery: jumps straight to the original power-down (no change).
 *  - On cable:   shows the four-cell gauge and the percentage, dims after
 *                SHOW_MS, wakes on a short power-key press, boots on a long
 *                press, and does the real power-down once the cable is pulled.
 *
 * v3 (after the first device test rebooted into USB mode on a wall charger):
 * the hook no longer draws text or calls RECHO's key/ADC poll. Text rendering
 * fetches glyphs from flash (fw1 0x3058978 -> sector read 0x3052806), which
 * is not safe once the power-off routine has stopped the player core. Digits
 * are now drawn from rectangles into the RAM frame buffer, and the battery is
 * sampled straight from the SAR ADC (0x400D0000, channel 0) the same way
 * RECHO's own poll at 0x30583a8 does it.
 *
 * Every firmware call left is one the original power-off routine itself uses
 * (fill, flush, backlight, delay) or a plain register read.
 * No .data/.bss: all state is on the stack.
 */
typedef unsigned int u32;

#ifndef HOST_TEST
#define FN(addr, type) ((type)((addr) | 1u))

#define power_down      FN(0x03051E28, void (*)(void))            /* noreturn */
#define reboot          FN(0x03051C84, void (*)(void))            /* noreturn */
#define ms_now          FN(0x03051638, u32 (*)(void))             /* SysTick counter */
#define delay_ms        FN(0x03051C48, void (*)(u32))             /* also feeds the watchdog */
#define cable_present   FN(0x03051C60, u32 (*)(void))             /* register read */
#define power_key_down  FN(0x03051C70, u32 (*)(void))             /* GPIO2.5 */
#define gpio_read       FN(0x03051610, u32 (*)(u32 bank, u32 pin))
#define last_battery_mv FN(0x0305837C, u32 (*)(void))             /* last value the UI measured */
#define mv_to_permille  FN(0x0306A4F0, int (*)(u32))              /* table lookup */
#define screen_all      FN(0x0306BF14, void (*)(void))            /* dirty = whole screen */
#define fill_rect       FN(0x0306BF2C, void (*)(int, int, int, int, u32)) /* RAM frame buffer */
#define lcd_flush       FN(0x0306BC70, void (*)(void))
#define backlight       FN(0x0306BDD4, void (*)(int))

#define BRIGHTNESS      (*(volatile int *)0x030041D4)
#define ADC             ((volatile u32 *)0x400D0000)              /* [0] data, [2] control */
#endif

#define SHOW_MS        8000u
#define REFRESH_MS     2000u
#define LONG_PRESS_MS  1200u

#define BLACK  0x0000u
#define GREY   0x8410u
#define WHITE  0xF79Eu
#define GREEN  0x07E0u
#define AMBER  0xFD20u
#define RED    0xF800u

/* ---- battery ------------------------------------------------------------ */

static u32 adc_battery_mv(void)
{
    /* Same register sequence as RECHO's poll (fw1 0x3058400..0x305843e), channel 0. */
    u32 sum = 0;
    for (int i = 0; i < 4; i++) {
        u32 c = ADC[2];
        c &= ~0x40u;
        ADC[2] = c;
        c &= ~0x10u;
        ADC[2] = c;
        ADC[2] = (c & ~0x47u) | 0x18u;
        delay_ms(10);
        sum += ADC[0] & 0x3FFu;
    }
    u32 raw = sum / 4;
    return raw * 5000u >> 10;
}

static int read_level(int *full)
{
    u32 mv = adc_battery_mv();
    if (mv < 2500u)              /* ADC not answering: use the last UI reading */
        mv = last_battery_mv();
    *full = gpio_read(2, 3) != 0;
    return mv ? mv_to_permille(mv) : -1;
}

/* ---- drawing (rectangles only) ----------------------------------------- */

#ifdef ECHO_PORT                          /* the Echo shows this picture small, centred on its panel */
#define CELL_W 28
#define CELL_H 52
#define CELL_GAP 6
#define CHAMFER 4
#define LINE 3
#else                                     /* Echo Mini: about 60 %, centred on its own screen */
#define CELL_W 17
#define CELL_H 31
#define CELL_GAP 4
#define CHAMFER 3
#define LINE 2
#endif

static void pill(int x, int y, int solid, u32 colour)
{
    int x1 = x + CELL_W - 1, y1 = y + CELL_H - 1;
    if (solid) {
        fill_rect(x + CHAMFER, y, x1 - CHAMFER, y1, colour);
        fill_rect(x, y + CHAMFER, x1, y1 - CHAMFER, colour);
        return;
    }
    fill_rect(x + CHAMFER, y, x1 - CHAMFER, y + LINE - 1, colour);
    fill_rect(x + CHAMFER, y1 - LINE + 1, x1 - CHAMFER, y1, colour);
    fill_rect(x, y + CHAMFER, x + LINE - 1, y1 - CHAMFER, colour);
    fill_rect(x1 - LINE + 1, y + CHAMFER, x1, y1 - CHAMFER, colour);
}

#ifdef ECHO_PORT
#define DIG_W 20
#define DIG_H 38
#define DIG_T 5
#define DIG_GAP 8
#define PCT_W 22
#define PCT_DOT 6
#define GAUGE_Y 26
#define DIGITS_Y 100
#else
#define DIG_W 12
#define DIG_H 23
#define DIG_T 3
#define DIG_GAP 5
#define PCT_W 13
#define PCT_DOT 4
#define GAUGE_Y 50                        /* 31 gauge + 16 gap + 23 digits = 70 rows, centred in 170 */
#define DIGITS_Y 97
#endif

static const unsigned char SEGMENTS[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, /* bits a b c d e f g */
};

static void digit(int x, int y, int d, u32 c)
{
    const int x1 = x + DIG_W - 1, y1 = y + DIG_H - 1, m = y + DIG_H / 2;
    const unsigned s = SEGMENTS[d];
    if (s & 0x01) fill_rect(x, y, x1, y + DIG_T - 1, c);                       /* a */
    if (s & 0x02) fill_rect(x1 - DIG_T + 1, y, x1, m, c);                      /* b */
    if (s & 0x04) fill_rect(x1 - DIG_T + 1, m, x1, y1, c);                     /* c */
    if (s & 0x08) fill_rect(x, y1 - DIG_T + 1, x1, y1, c);                     /* d */
    if (s & 0x10) fill_rect(x, m, x + DIG_T - 1, y1, c);                       /* e */
    if (s & 0x20) fill_rect(x, y, x + DIG_T - 1, m, c);                        /* f */
    if (s & 0x40) fill_rect(x, m - DIG_T / 2, x1, m - DIG_T / 2 + DIG_T - 1, c); /* g */
}

static void percent_sign(int x, int y, u32 c)
{
    fill_rect(x, y + 2, x + PCT_DOT - 1, y + PCT_DOT + 1, c);
    fill_rect(x + PCT_W - PCT_DOT, y + DIG_H - PCT_DOT - 2, x + PCT_W - 1, y + DIG_H - 3, c);
    for (int i = 0; i < 8; i++) {               /* stepped diagonal, bottom-left to top-right */
        int sx = x + 1 + i * (PCT_W - 4) / 8;
        int sy = y + DIG_H - 1 - (i + 1) * DIG_H / 8;
        fill_rect(sx, sy, sx + PCT_DOT / 2 + 1, sy + DIG_H / 8, c);
    }
}

static void draw(int permille, int full)
{
    int pct = permille < 0 ? 0 : (permille > 1000 ? 100 : permille / 10);
    if (full)
        pct = 100;
    int lit = (pct + 24) / 25; /* 1-25 % -> 1 cell ... 76-100 % -> 4 cells */
    u32 colour = full ? GREEN : (pct <= 15 ? RED : (pct <= 35 ? AMBER : WHITE));

    screen_all();
    fill_rect(0, 0, 319, 169, BLACK);

    const int total = 4 * CELL_W + 3 * CELL_GAP;
    const int gx = (320 - total) / 2, gy = GAUGE_Y;
    for (int c = 0; c < 4; c++)
        pill(gx + c * (CELL_W + CELL_GAP), gy, c < lit, c < lit ? colour : GREY);

    int digits[3], n = 0, v = pct;
    do {
        digits[n++] = v % 10;
        v /= 10;
    } while (v && n < 3);
    int width = n * (DIG_W + DIG_GAP) + PCT_W;
    int x = (320 - width) / 2, y = DIGITS_Y;
    for (int i = n - 1; i >= 0; i--) {
        digit(x, y, digits[i], full ? GREEN : WHITE);
        x += DIG_W + DIG_GAP;
    }
    percent_sign(x, y, full ? GREEN : WHITE);
#ifdef ECHO_PORT
    {   /* full-size Echo: show the 320 x 170 picture in the middle of the 480 x 222 panel */
        extern void echo_flush_centred(void);
        echo_flush_centred();
    }
#else
    lcd_flush();
#endif
}

/* ---- main loop ---------------------------------------------------------- */

void charge_screen(void)
{
    extern void bt_shutdown(void);   /* bt.c: Bluetooth module off */
    bt_shutdown();
    if (!cable_present())
        power_down();

    int level = BRIGHTNESS;
    if (level <= 0)
        level = 5;

#ifdef ECHO_PORT
    {   /* full-size Echo: blank the Up Next column and the margins first */
        FN(0x0306B534, void (*)(void))();              /* no DMA band in flight */
        extern void echo_panel_fill(unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
        echo_panel_fill(0, 0, 479, 319, 0);                /* whole panel black */
    }
#endif
    int full;
    int permille = read_level(&full);
    backlight(level);
    draw(permille, full);

    u32 shown_at = ms_now();
    u32 drawn_at = shown_at;
    int lit = 1;
    u32 pressed_at = 0;
    int pressed = 0;
    int armed = 0; /* power-off may have come from holding the key: wait for release */

    for (;;) {
        delay_ms(20);
        u32 now = ms_now();

        if (!cable_present()) {
            backlight(0);
            power_down();
        }

        if (!armed) {
            if (!power_key_down())
                armed = 1;
        } else if (power_key_down()) {
            if (!pressed) {
                pressed = 1;
                pressed_at = now;
            } else if (now - pressed_at >= LONG_PRESS_MS) {
                backlight(0);
                reboot();
            }
        } else if (pressed) {
            pressed = 0;
            permille = read_level(&full);
            if (!lit)
                backlight(level);
            draw(permille, full);
            lit = 1;
            shown_at = drawn_at = now;
        }

        if (lit) {
            if (now - shown_at >= SHOW_MS) {
                backlight(0);
                lit = 0;
            } else if (now - drawn_at >= REFRESH_MS) {
                permille = read_level(&full);
                draw(permille, full);
                drawn_at = now;
            }
        }
    }
}
