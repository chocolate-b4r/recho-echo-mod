/*
 * Button remap for RECHO 0.10.1 (2d11bdcd) — shared by the Echo Mini and full-size Echo builds.
 *
 *   ↩ tap   on Now Playing: Back to the main menu. m45's play-mode cycle at 0x307A12C becomes
 *           `bl 0x305BA3C` (RECHO's own "go to the main menu") + `b.n 0x307A170` (its common tail).
 *           Play mode stays in Settings -> Play mode.
 *   ↩ hold  previous track (unchanged).
 *   ≡ hold  Settings while Now Playing is open, the main menu everywhere else: RECHO's global
 *           hold-menu call at 0x305BF18 (`bl 0x305BA3C`) becomes `bl hold_menu`.
 *   labels  m45's soft-key literals for "MODE" (0x307A1E8) and "menu" (0x307A1D8) point at
 *           str_back / str_settings, so the bar (where shown) reads BACK and hold settings.
 */
typedef unsigned int u32;

static inline u32 lit(u32 v)
{
    __asm__("" : "+r"(v));
    return v;
}
#define FN(addr, type) ((type)lit((addr) | 1u))
#define REG(addr)      (*(volatile u32 *)lit(addr))
#define STATE __attribute__((section(".text.keys_state,\"ax\",%progbits@"), used))

#define CURRENT_MODULE  REG(0x030399C8)                 /* id of the loaded screen module */
#define set_module      FN(0x0305B234, void (*)(int module))
#define go_main_menu    FN(0x0305BA3C, void (*)(void))
#define MOD_NOW_PLAYING 45u
#define MOD_SETTINGS    48

const char str_back[] = "BACK";
const char str_settings[] = "settings";

void hold_menu(void)
{
    if (CURRENT_MODULE == MOD_NOW_PLAYING)
        set_module(MOD_SETTINGS);
    else
        go_main_menu();
}

/*
 * USB disk: ↩ (Back) leaves, as in the stock firmware.
 * RECHO's USB-disk loop polls one callback (fw1 0x30505EC); once armed it leaves on
 * `power_key_down()` at 0x305062C, which this replaces. RECHO's key poll does not run in
 * that loop, so ↩ is read straight from the key ladder (SAR ADC channel 1, same register
 * sequence as RECHO's poll at 0x3058400). A conversion is started on one call and read on
 * a later one (>= 20 ms apart), so the loop never waits for the ADC. Whatever key leaves,
 * the hook waits for it to be released first: the UI then sees no press afterwards (before,
 * the power press that left also reached the UI and switched the screen off).
 */
#define ms_now         FN(0x03051638, u32 (*)(void))
#define delay_ms       FN(0x03051C48, void (*)(u32))          /* also feeds the watchdog */
#define power_key_down FN(0x03051C70, u32 (*)(void))
#define ADC            ((volatile u32 *)lit(0x400D0000u))      /* [0] data, [2] control */
#define BACK_LO 450u                                           /* RECHO's own ↩ band (0x305850E): 450..549 */
#define BACK_HI 549u

STATE static volatile u32 usb_adc_at = 0, usb_adc_chan = 0xFFFFFFFFu, usb_back_hits = 0;

static void adc_start(u32 chan)
{
    u32 c = ADC[2];
    c &= ~0x40u;
    ADC[2] = c;
    c &= ~0x10u;
    ADC[2] = c;
    ADC[2] = (c & ~0x47u) | (chan & 7u) | 0x18u;
}

static int back_now(void)                                     /* blocking read, for the release wait */
{
    adc_start(1);
    delay_ms(2);
    u32 v = ADC[0] & 0x3FFu;
    return v >= BACK_LO && v <= BACK_HI;
}

u32 usb_key_exit(void)
{
    int power = power_key_down() != 0, back = 0;
    if (!power) {
        u32 now = ms_now();
        if (usb_adc_chan == 0xFFFFFFFFu) {                    /* first call: remember RECHO's channel */
            usb_adc_chan = ADC[2] & 7u;
            usb_back_hits = 0;
            adc_start(1);
            usb_adc_at = now;
            return 0;
        }
        if (now - usb_adc_at < 20u)
            return 0;
        u32 v = ADC[0] & 0x3FFu;                              /* the conversion started last time */
        usb_back_hits = (v >= BACK_LO && v <= BACK_HI) ? usb_back_hits + 1u : 0u;
        adc_start(1);
        usb_adc_at = now;
        back = usb_back_hits >= 2u;                           /* two readings in a row, 20 ms apart */
        if (!back)
            return 0;
    }
    for (u32 t = 0; t < 3000u; t += 10u) {                    /* wait for the release (3 s at most) */
        if (power ? !power_key_down() : !back_now())
            break;
        delay_ms(10);
    }
    adc_start(usb_adc_chan == 0xFFFFFFFFu ? 0u : usb_adc_chan);   /* RECHO's poll continues where it was */
    usb_adc_chan = 0xFFFFFFFFu;
    return 1;
}

/*
 * Main menu: hold ▶‖ on the Files tab plays everything on the internal storage, as holding ▶‖
 * on "Music" at the top of Files does. Only with the screen on (the menu gets no keys with the
 * screen off; RECHO's own hold ▶‖ = next track still applies there).
 *   menu_key      replaces the main menu descriptor's key handler (m47 0x307AAA4 = 0x307A109):
 *                 hold ▶‖ on tab 0 (Files) -> open Files (m44) and remember the request;
 *                 every other key goes to RECHO's handler unchanged.
 *   play_all_tick (POWER tick) once m44 is loaded and has had 300 ms to open, and only if the
 *                 code at 0x307A278 is m44's folder-tree routine byte for byte: select drive 0
 *                 (0x305A02C) and call it as m44's own hold ▶‖ does: 0x307A278(0, "Music"). It
 *                 queues the whole drive, starts playback and opens Now Playing.
 */
#define menu_key_orig  FN(0x0307A108, int (*)(const unsigned char *ev))   /* m47's own handler */
#define select_volume  FN(0x0305A02C, int (*)(int drive))
#define m44_tree       FN(0x0307A278, void (*)(int dir, const char *name))
#define MENU_CURSOR    REG(0x0303BCC4)                                       /* main menu state [1] */
#define BL_DUTY        REG(0x03006174)                                       /* backlight PWM, 0 = screen off */
#define M44_SD_FLAG    REG(0x0303AB2C)                                       /* m44: "the drive is the SD card" */
#define OVERLAY        ((const volatile u32 *)lit(0x0307A000u))             /* loaded module: "SFMD", id */

STATE static volatile u32 play_all_pending = 0, play_all_at = 0, play_all_seen = 0;

int menu_key(const unsigned char *ev)
{
    if (ev[0] == 4 && ev[1] == 3 && MENU_CURSOR == 0u && BL_DUTY != 0u) {
        play_all_pending = 1;
        play_all_seen = 0;
        play_all_at = ms_now();
        set_module(44);
        return 1;
    }
    return menu_key_orig(ev);
}

void play_all_tick(void)
{
    if (!play_all_pending)
        return;
    u32 now = ms_now();
    if (now - play_all_at > 4000u) {                 /* Files never opened: forget it */
        play_all_pending = 0;
        return;
    }
    if (CURRENT_MODULE != 44u || OVERLAY[0] != 0x444D4653u || OVERLAY[1] != 44u)
        return;
    if (!play_all_seen) {
        play_all_seen = 1;
        play_all_at = now;                           /* give m44 300 ms to open */
        return;
    }
    if (now - play_all_at < 300u)
        return;
    play_all_pending = 0;
    const volatile u32 *code = (const volatile u32 *)lit(0x0307A278u);
    if (code[0] != 0x4FF0E92Du || code[1] != 0x9103B08Du || code[2] != 0xF7D74605u || code[3] != 0x9007F9D9u)
        return;                                      /* not RECHO 0.10.1's m44: do nothing */
    if (!select_volume(0))
        return;
    M44_SD_FLAG = 0;
    m44_tree(0, "Music");
}
