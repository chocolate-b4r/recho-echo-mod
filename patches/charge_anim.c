/*
 * Status-bar charging animation for RECHO 0.10.1 (build 2d11bdcd).
 *
 * While the cable is in and the charger has not raised its "done" pin, the
 * battery glyph cycles 0 -> 4 lit cells every ANIM_MS. When done, or on
 * battery, the glyph shows the measured level exactly as before.
 *
 * Two hook points:
 *  1. status_level_shim — replaces the 6 bytes at 0x305945E in the status-bar
 *     draw (`ldr r2,[r3,#8]` level, `ldr.w r8,[r3,#20]` low flag). It returns
 *     the level in r2 and keeps r8 and r3 exactly as the original did.
 *  2. power_tick_wrap — replaces the POWER service's tick pointer at
 *     0x306AC68. It runs the original tick (0x306A7C4) and returns its result
 *     unchanged, then asks the current screen to redraw when a new animation
 *     frame starts — the same request RECHO's own screens make (0x305C51C).
 *
 * The battery state block at 0x030401EC is RECHO's own:
 *   [0] mV, [1] per-mille, [2] level 0..4, [3] cable, [4] charger done, [5] low.
 */
typedef unsigned int u32;

/* The empty asm stops the compiler from folding firmware addresses into
 * base+offset arithmetic, so each one stays a literal the verifier can audit. */
static inline u32 lit(u32 v)
{
    __asm__("" : "+r"(v));
    return v;
}
#define FN(addr, type) ((type)lit((addr) | 1u))
#define ms_now          FN(0x03051638, u32 (*)(void))
#define power_tick_orig FN(0x0306A7C4, u32 (*)(void))
#define request_redraw  FN(0x0305C51C, void (*)(const void *screen))

#define BATTERY         ((volatile int *)lit(0x030401ECu))
#define CURRENT_SCREEN  (*(const void *volatile *)lit(0x0303E73Cu))

#define ANIM_MS 500u

/* One word of state, kept in the cave (code RAM is plain SRAM; no MPU). */
__attribute__((section(".text.anim_state,\"ax\",%progbits@"), used)) volatile u32 anim_last_frame = 0;

static int charging(void)
{
    return BATTERY[3] != 0 && BATTERY[4] == 0;
}

u32 anim_level(const volatile int *bat)
{
    if (bat[3] != 0 && bat[4] == 0)
        return (ms_now() / ANIM_MS) % 5u;
    return (u32)bat[2];
}

u32 power_tick_wrap(void)
{
    u32 busy = power_tick_orig();
    u32 frame = 0;
    if (charging())
        frame = ms_now() / ANIM_MS + 1u;          /* never 0 while charging */
    if (frame != anim_last_frame) {
        anim_last_frame = frame;
        const void *screen = CURRENT_SCREEN;
        if (screen)
            request_redraw(screen);
    }
    {
        extern void clock_tick(void);     /* clock.c: top bars follow the minute */
        extern void bt_tick(void);        /* bt.c: Bluetooth module and screen */
        extern void about_tick(void);     /* ui_np.c: Settings -> About */
        clock_tick();
        bt_tick();
        about_tick();
    }
#ifdef ECHO_PORT
    {
        extern void echo_hatch_tick(void);
        extern void echo_sidebar_tick(void);
        extern void echo_band_tick(void);
        echo_hatch_tick();
        echo_sidebar_tick();
        echo_band_tick();
    }
#endif
    return busy;
}

/* Called with `bl` from the status-bar draw; r3 = battery block on entry. */
__attribute__((naked)) void status_level_shim(void)
{
    __asm__ volatile(
        "push   {r3, lr}\n"
        "mov    r0, r3\n"
        "bl     anim_level\n"
        "mov    r2, r0\n"
        "pop    {r3, lr}\n"
        "ldr.w  r8, [r3, #20]\n"
        "bx     lr\n");
}
