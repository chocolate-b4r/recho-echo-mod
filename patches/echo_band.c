/*
 * RECHO on the full-size Echo — clock and the button bar (stage 5).
 *
 * Button bar: removed on the Echo (keys are learnt once; see docs). RECHO's own bar
 * messages are shown on one line at panel rows 245..270, under the centred screen.
 *
 * Clock: RECHO's own clock-chip routines (Settings -> Clock uses them):
 *   0x305DA2C(regs[16]) reads (0 = ok); 0x305DAC4(regs, &t) decodes u16 year, u8 month,
 *   day, hour, min, sec, weekday (0 = Sunday), returns 1 if the chip's time is unreliable.
 * Read at boot/wake and every 5 minutes; counted from RECHO's ms clock in between.
 * When the minute changes the current screen is asked to redraw (its top bar shows it).
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

static inline u32 lit(u32 v)
{
    __asm__("" : "+r"(v));
    return v;
}
#define FN(addr, type) ((type)lit((addr) | 1u))
#define REG(addr)      (*(volatile u32 *)lit(addr))
#define PTR(type, a)   ((type)lit(a))

#define push_idle      FN(0x0306B534, void (*)(void))
#define fill_rect      FN(0x0306BF2C, void (*)(int, int, int, int, u32))
#define text_width     FN(0x03058D20, int (*)(int font, const char *s))
#define ms_now         FN(0x03051638, u32 (*)(void))
#define rtc_read       FN(0x0305DA2C, int (*)(u8 *regs))
#define rtc_decode     FN(0x0305DAC4, int (*)(const u8 *regs, void *t))
#define current_screen FN(0x0305C41C, const volatile u32 *(*)(void))
#define bar_message    FN(0x0305AE28, const char *(*)(void))
#define request_redraw FN(0x0305C51C, void (*)(const void *screen))

#define VOP_ACTIVE  REG(0x030089EC)
#define BL_DUTY     REG(0x03006174)
#define FB          PTR(volatile u16 *, 0x03008A00)

#define BAR_Y       245
#define BAR_H       26
#define MSG_W       320
#define RESYNC_MS   300000u

#define WHITE 0xF79Eu
#define SOFT  0xBDD7u
#define GREY  0x8410u
#define RULE  0x4208u
#define AMBER 0xFD20u

struct rtc_time { u16 year; u8 month, day, hour, min, sec, wday; };

#define STATE __attribute__((section(".text.band_state,\"ax\",%progbits@"), used))
STATE volatile u32 band_sync_ms = 0;
STATE volatile u32 band_sync_sec = 0;
STATE volatile u32 band_sync_ok = 0;
STATE volatile u32 band_ymd = 0;
STATE volatile u32 band_wday = 0;
STATE volatile u32 band_need_sync = 1;
STATE volatile u32 band_shown_min = 0xFFFFFFFFu;
STATE volatile u32 bar_shown_sum = 0xFFFFFFFFu;      /* checksum of the bar on the panel; 0 = blank */

extern volatile u32 sb_blank;
extern volatile u32 hints_dirty;
extern void ui_borrow(int w, int h);
extern void ui_return(int px, int py, int w, int h);
extern void ui_text(int x, int y_mid, const char *text, int maxw, u32 colour);
extern void echo_panel_fill(u32 x0, u32 y0, u32 x1, u32 y1, u32 colour);

/* ---- clock ---------------------------------------------------------------------- */

static void sync_clock(u32 now)
{
    u8 regs[16];
    struct rtc_time t;
    band_need_sync = 0;
    band_sync_ms = now;
    if (rtc_read(regs) != 0) {
        band_sync_ok = 0;
        return;
    }
    int unreliable = rtc_decode(regs, &t);
    if (t.hour > 23 || t.min > 59 || t.sec > 59 || t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31) {
        band_sync_ok = 0;
        return;
    }
    band_sync_ok = unreliable ? 2 : 1;
    band_sync_sec = (u32)t.hour * 3600u + (u32)t.min * 60u + t.sec;
    band_ymd = ((u32)t.year << 16) | ((u32)t.month << 8) | t.day;
    band_wday = t.wday % 7u;
}

static u32 minutes_now(u32 now)
{
    if (!band_sync_ok)
        return 0xFFFFu;
    u32 sec = band_sync_sec + (now - band_sync_ms) / 1000u;
    if (sec >= 86400u) {
        band_need_sync = 1;
        sec %= 86400u;
    }
    return sec / 60u;
}

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *put2(char *p, u32 v)
{
    *p++ = (char)('0' + (v / 10u) % 10u);
    *p++ = (char)('0' + v % 10u);
    return p;
}

/* "21:47" and "Thu 12 Nov" (or "--:--" and "set the clock") for the top bars */
void clock_strings(char *time, char *date)
{
    static const char DAYS[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char MONTHS[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    u32 now = ms_now();
    if (band_need_sync || now - band_sync_ms >= RESYNC_MS)
        sync_clock(now);
    u32 mins = minutes_now(now);
    char *p = time;
    if (mins == 0xFFFFu) {
        p = put_str(p, "--:--");
    } else {
        p = put2(p, mins / 60u);
        *p++ = ':';
        p = put2(p, mins % 60u);
    }
    *p = 0;
    p = date;
    if (band_sync_ok) {
        u32 day = band_ymd & 0xFFu, mon = (band_ymd >> 8) & 0xFFu;
        p = put_str(p, DAYS[band_wday % 7u]);
        *p++ = ' ';
        if (day >= 10u)
            *p++ = (char)('0' + day / 10u);
        *p++ = (char)('0' + day % 10u);
        *p++ = ' ';
        p = put_str(p, MONTHS[(mon - 1u) % 12u]);
    } else {
        p = put_str(p, "set the clock");
    }
    *p = 0;
}

/* ---- button bar ------------------------------------------------------------------ */

/* RECHO's bar messages ("headphones out: paused", volume limits, ...) get one centred
 * line under the screen; nothing is shown otherwise (no button labels on the Echo). */
static void draw_bar(void)
{
    const char *msg = bar_message();
    u32 sum = 0;
    if (msg && *msg) {
        sum = 0x811C9DC5u;
        for (const char *p = msg; *p; p++)
            sum = (sum ^ (u8)*p) * 16777619u;
        if (sum == 0)
            sum = 1;
    }
    if (sum == bar_shown_sum)
        return;
    echo_panel_fill(0, BAR_Y, 479, BAR_Y + BAR_H - 1, 0);
    if (sum) {
        ui_borrow(MSG_W, BAR_H);
        int w = text_width(1, msg);
        if (w > MSG_W - 8)
            w = MSG_W - 8;
        ui_text((MSG_W - w) / 2, BAR_H / 2, msg, MSG_W - 8, AMBER);
        ui_return((480 - MSG_W) / 2, BAR_Y, MSG_W, BAR_H);
    }
    bar_shown_sum = sum;
}

/* ---- tick ---------------------------------------------------------------------------- */

void echo_band_tick(void)
{
    if (!VOP_ACTIVE || BL_DUTY == 0 || sb_blank)
        return;
    u32 now = ms_now();
    if (band_need_sync || now - band_sync_ms >= RESYNC_MS)
        sync_clock(now);
    u32 mins = minutes_now(now);
    if (mins != band_shown_min) {                         /* top bars show the time */
        band_shown_min = mins;
        const volatile u32 *screen = current_screen();
        if (screen)
            request_redraw((const void *)screen);
    }
    if (hints_dirty) {
        hints_dirty = 0;
        push_idle();
        draw_bar();
    }
}

void echo_band_invalidate(void)
{
    band_need_sync = 1;
    bar_shown_sum = 0xFFFFFFFFu;
    hints_dirty = 1;
}
