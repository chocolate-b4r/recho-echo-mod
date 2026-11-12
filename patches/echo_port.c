/*
 * RECHO 0.10.1 (2d11bdcd) on the full-size Snowsky Echo — stage 1 hooks.
 *
 * Every Echo value below comes from Podium 4.6.0 (GPL, EchoPod fork for the
 * full-size Echo), which took them from FiiO's Echo 2.0.0 firmware:
 *   panel   platform/device/logic/panel.c   (ST7796S_STEPS, rows +49, MADCTL 0x28)
 *   pins    platform/device/logic/board_pins.c, board/power.c, board/jack.c
 *   maskrom platform/device/board/loader_reboot.c
 *
 * Hooks (applied by tools/patch_recho.py --echo):
 *   echo_panel_init   replaces both `bl 0x306B000` (Mini panel init) after sleep-out
 *   gpio_set_echo     entered by `b.w` from the first 4 bytes of 0x3051590 (GPIO set)
 *   echo_gpio_read    replaces the jack reads of GPIO2.8 / GPIO2.0 (0x3055EE8, 0x3055F6A)
 *   echo_hatch_tick   called from the POWER tick wrapper: hold Menu 10 s -> maskrom
 */
typedef unsigned int u32;
typedef unsigned short u16;

static inline u32 lit(u32 v)
{
    __asm__("" : "+r"(v));
    return v;
}
#define FN(addr, type) ((type)lit((addr) | 1u))
#define REG(addr)      (*(volatile u32 *)lit(addr))

#define panel_send   FN(0x0306AF98, void (*)(u32 cmd, const unsigned char *data, u32 len))
#define panel_mode   FN(0x0306AF6C, void (*)(u32 mode))          /* 0x800 command, 2 pixel */
#define delay_ms     FN(0x03051C48, void (*)(u32))
#define gpio_read    FN(0x03051610, u32 (*)(u32 bank, u32 pin))
#define ms_now       FN(0x03051638, u32 (*)(void))
#define VOP_ACTIVE   REG(0x030089EC)                             /* RECHO: panel on the VOP bus */
#define VOP_STATUS   REG(0x60070024)
#define VOP_DATA     REG(0x6007002C)
#define KEY_ADC_RAW  REG(0x030041A8)                             /* RECHO: last key-ladder reading */

/* ---- GPIO set: Mini pin meanings -> Echo pins ---------------------------- */

/* Original GPIO set with its first two instructions replayed, then back into RECHO. */
__attribute__((naked)) void gpio_set_orig(u32 bank, u32 pin, u32 level)
{
    (void)bank; (void)pin; (void)level;
    __asm__ volatile(
        "push   {r4, r5, r6, lr}\n"
        "mov    r5, r1\n"
        "ldr.w  pc, 1f\n"
        ".align 2\n"
        "1: .word 0x03051595\n");
}

#define STATE_P __attribute__((section(".text.port_state,\"ax\",%progbits@"), used))
STATE_P volatile u32 echo_mute_level = 0;  /* RECHO's last DAC-mute request */
STATE_P volatile u32 echo_wired_level = 1; /* RECHO's last wired-output request (GPIO2.14) */
extern volatile u32 bt_routed;             /* ui_np.c / bt.c: music goes to Bluetooth */

void gpio_set_echo(u32 bank, u32 pin, u32 level)
{
    if (bank == 2 && pin == 0) {          /* Mini DAC mute -> Echo DAC mute GPIO2.1 */
        echo_mute_level = level;
        gpio_set_orig(2, 1, bt_routed ? 1u : level);
        return;
    }
    if (bank == 2 && pin == 14) {         /* wired output: kept off while Bluetooth plays */
        echo_wired_level = level;
        gpio_set_orig(2, 14, bt_routed == 1u ? 0u : level);   /* 2: test variant keeps it */
        return;
    }
    if (bank == 2 && pin == 1)            /* Mini DAC reset: the Echo has none */
        return;
    gpio_set_orig(bank, pin, level);
}

u32 echo_gpio_read(u32 bank, u32 pin)
{
    if (bank == 2 && pin == 8) {          /* 4.4 mm: Echo reads high when plugged */
        /* RECHO only sets the 3.5 mm pin's pull (GRF +0x14). Without GPIO2.8's pull
         * (GRF +0x54 bit 0, as stock and Podium set it) the pin floats and reads "plugged". */
        REG(0x50010054) = 0x00010001u;
        return gpio_read(2, 8) ? 0u : 1u;
    }
    if (bank == 2 && pin == 0)            /* mute state lives on GPIO2.1 */
        return gpio_read(2, 1);
    return gpio_read(bank, pin);
}

/* ---- panel: ST7796S, 480 x 222 landscape ---------------------------------- */

static const unsigned char P_F0A[] = {0xC3}, P_F0B[] = {0x96}, P_36[] = {0x28}, P_3A[] = {0x55},
    P_B4[] = {0x01}, P_B6[] = {0x20, 0x02, 0x3B}, P_B7[] = {0xC6}, P_B9[] = {0x02, 0xE0},
    P_C0[] = {0xC0, 0x64}, P_C1[] = {0x1D}, P_C2[] = {0xA7}, P_C5[] = {0x28},
    P_E8[] = {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33},
    P_E0[] = {0xF0, 0x0B, 0x12, 0x09, 0x0A, 0x26, 0x39, 0x54, 0x4E, 0x38, 0x13, 0x13, 0x2E, 0x34},
    P_E1[] = {0xF0, 0x10, 0x15, 0x0D, 0x0C, 0x07, 0x38, 0x43, 0x4D, 0x3A, 0x16, 0x15, 0x30, 0x35},
    P_F0C[] = {0x3C}, P_F0D[] = {0x69}, P_35[] = {0x00};

struct step { unsigned char cmd, len; const unsigned char *data; };
void echo_panel_fill(u32 x0, u32 y0, u32 x1, u32 y1, u32 colour);

static const struct step ST7796S[] = {
    {0xF0, 1, P_F0A}, {0xF0, 1, P_F0B}, {0x36, 1, P_36}, {0x3A, 1, P_3A}, {0xB4, 1, P_B4},
    {0xB6, 3, P_B6},  {0xB7, 1, P_B7},  {0xB9, 2, P_B9}, {0xC0, 2, P_C0}, {0xC1, 1, P_C1},
    {0xC2, 1, P_C2},  {0xC5, 1, P_C5},  {0xE8, 8, P_E8}, {0xE0, 14, P_E0}, {0xE1, 14, P_E1},
    {0xF0, 1, P_F0C}, {0xF0, 1, P_F0D}, {0x35, 1, P_35}, {0x21, 0, 0},
};

/* Called where RECHO calls its Mini init: sleep-out (0x11) was sent 20 ms ago,
 * display-on (0x29) and memory-write (0x2C) follow when we return. */
void echo_panel_init(void)
{
    /* Echo power pair (stock and Podium hold it high from start-up). */
    gpio_set_orig(0, 14, 1);
    gpio_set_orig(2, 0, 1);

    delay_ms(100);                        /* ST7796S: 120 ms after sleep-out */
    for (unsigned i = 0; i < sizeof ST7796S / sizeof ST7796S[0]; i++)
        panel_send(ST7796S[i].cmd, ST7796S[i].data, ST7796S[i].len);

    /* Clear all of the panel's memory (480 x 320, of which rows 49..270 are visible):
     * RECHO only ever draws 320 x 170 of it. At boot the panel can still be on the
     * slow fallback bus here (VOP not active yet); then the sidebar tick clears the
     * parts RECHO never draws as soon as the VOP is up. */
    extern void echo_sidebar_invalidate(void);
    echo_sidebar_invalidate();
    if (VOP_ACTIVE)
        echo_panel_fill(0, 0, 479, 319, 0);
}

/* Fill a rectangle of panel memory directly (panel coordinates, MADCTL 0x28).
 * Callers make sure no DMA band is in flight. Does nothing without the VOP. */
void echo_panel_fill(u32 x0, u32 y0, u32 x1, u32 y1, u32 colour)
{
    if (!VOP_ACTIVE)
        return;
    unsigned char c[4] = {(unsigned char)(x0 >> 8), (unsigned char)x0, (unsigned char)(x1 >> 8), (unsigned char)x1};
    unsigned char r[4] = {(unsigned char)(y0 >> 8), (unsigned char)y0, (unsigned char)(y1 >> 8), (unsigned char)y1};
    panel_send(0x2A, c, 4);
    panel_send(0x2B, r, 4);
    panel_send(0x2C, 0, 0);
    panel_mode(2);
    for (u32 n = (x1 - x0 + 1) * (y1 - y0 + 1); n; n--) {
        VOP_DATA = colour;
        if ((n & 3u) == 0)
            for (u32 t = 2000; (VOP_STATUS & 1u) && t; t--)
                ;
    }
    for (u32 t = 2000; (VOP_STATUS & 1u) && t; t--)
        ;
}

/* Push full-width frame-buffer rows fb_y.. to panel rows panel_y.. (callers wait for DMA first). */
void echo_push_rows(int fb_y, int panel_y, int rows)
{
    if (!VOP_ACTIVE)
        return;
    const int y1 = panel_y + rows - 1;
    unsigned char c[4] = {0, 0, 0x01, 0x3F};              /* columns 0..319 */
    unsigned char r[4] = {(unsigned char)(panel_y >> 8), (unsigned char)panel_y,
                          (unsigned char)(y1 >> 8), (unsigned char)y1};
    panel_send(0x2A, c, 4);
    panel_send(0x2B, r, 4);
    panel_send(0x2C, 0, 0);
    panel_mode(2);
    volatile u16 *p = (volatile u16 *)lit(0x03008A00u) + fb_y * 320;
    for (int n = rows * 320; n > 0; n -= 4) {
        VOP_DATA = p[0];
        VOP_DATA = p[1];
        VOP_DATA = p[2];
        VOP_DATA = p[3];
        p += 4;
        for (u32 t = 2000; (VOP_STATUS & 1u) && t; t--)
            ;
    }
    for (u32 t = 2000; (VOP_STATUS & 1u) && t; t--)
        ;
}

/* Off-charging screen: the whole 320 x 170 frame buffer, centred on the 480 x 222 panel
 * (columns 80..399, rows 49 + 26 .. 49 + 26 + 169). */
void echo_flush_centred(void)
{
    FN(0x0306B534, void (*)(void))();                    /* no DMA band in flight */
    if (!VOP_ACTIVE)
        return;
    unsigned char c[4] = {0x00, 80, 0x01, 0x8F};          /* columns 80..399 */
    unsigned char r[4] = {0x00, 75, 0x00, 244};           /* rows 75..244 */
    panel_send(0x2A, c, 4);
    panel_send(0x2B, r, 4);
    panel_send(0x2C, 0, 0);
    panel_mode(2);
    volatile u16 *p = (volatile u16 *)lit(0x03008A00u);
    for (int n = 320 * 170; n > 0; n -= 4) {
        VOP_DATA = p[0];
        VOP_DATA = p[1];
        VOP_DATA = p[2];
        VOP_DATA = p[3];
        p += 4;
        for (u32 t = 2000; (VOP_STATUS & 1u) && t; t--)
            ;
    }
    for (u32 t = 2000; (VOP_STATUS & 1u) && t; t--)
        ;
}

/* Black out every part of the panel RECHO never draws. Visible rows are 49..270.
 * RECHO owns cols 0..319, rows 49..218; the player strip is cols 0..319, rows 219..270
 * (echo_band.c); Up Next is cols 320..479, rows 49..270. */
void echo_blank_outside(void)
{
    echo_panel_fill(0, 0, 479, 48, 0);
    echo_panel_fill(0, 219, 479, 319, 0);
    echo_panel_fill(320, 49, 479, 218, 0);
}

/* ---- escape hatch: hold Menu for 10 s -> boot ROM USB loader (maskrom) ----- */

#define MENU_MIN 320u                     /* RECHO key ladder: play ~210-310, menu ~337-393, back ~480+ */
#define MENU_MAX 440u
#define HATCH_MS 10000u

__attribute__((section(".text.hatch_state,\"ax\",%progbits@"), used)) volatile u32 hatch_since = 0;

static void spin(volatile u32 n)
{
    while (n--)
        ;
}

static void loader_reboot(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
    REG(0xE000E010) = 0;                                  /* SysTick off */
    REG(0xE000E180) = 0xFFFFFFFFu;
    REG(0xE000E280) = 0xFFFFFFFFu;
    REG(0xE000E184) = 0xFFFFFFFFu;
    REG(0xE000E284) = 0xFFFFFFFFu;
    for (u32 off = 0x80; off <= 0xA4; off += 4)           /* CRU: all gates on */
        REG(0x40180000u + off) = 0xFFFF0000u;
    REG(0x40180000u + 0xA0) = 0x80008000u;
    spin(0x4B0);
    for (u32 off = 0xC0; off <= 0xCC; off += 4)           /* CRU: all resets released */
        REG(0x40180000u + off) = 0xFFFF0000u;
    REG(0x50010000u + 0xB8) = 0x00010000u;                /* PLL back to the ROM's state */
    REG(0x50010000u + 0xB4) = 0x000E0002u;
    REG(0x50010000u + 0x98) = 0x00040004u;
    REG(0x50010000u + 0xB4) = 0x00010000u;
    spin(100);
    REG(0x50010000u + 0xC4) = 0x01000000u;                /* GRF boot select */
    REG(0xE000ED08) = 0;                                  /* VTOR */
    REG(0x50020000u + 0x4C) = 0xFFFF18AFu;                /* PMU boot flag: USB loader */
    __asm__ volatile("dsb" ::: "memory");
    REG(0xE000ED0C) = 0x05FA0007u;                        /* system reset */
    for (;;)
        ;
}

void echo_hatch_tick(void)
{
    u32 raw = KEY_ADC_RAW;
    u32 now = ms_now() | 1u;              /* never 0, 0 means "not held" */
    if (raw < MENU_MIN || raw > MENU_MAX) {
        hatch_since = 0;
        return;
    }
    if (!hatch_since)
        hatch_since = now;
    else if (now - hatch_since >= HATCH_MS)
        loader_reboot();
}
