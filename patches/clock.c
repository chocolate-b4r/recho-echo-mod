/*
 * Clock for the top bars (both players).
 *
 * RECHO's own clock-chip routines, the ones Settings -> Clock uses:
 *   0x305DA2C(regs[16]) reads the chip (0 = ok)
 *   0x305DAC4(regs, &t) decodes u16 year, u8 month, day, hour, min, sec, weekday (0 = Sunday);
 *                       returns 1 when the chip says its time is not reliable
 * The chip is read at boot/wake, every minute and right after Settings closes (where the clock
 * is set); in between the time is counted from RECHO's millisecond clock. clock_tick() (main
 * loop) asks the current screen to redraw when the minute changes, so a paused Now Playing still
 * shows the right time. The weekday is worked out from the date: the chip's weekday register
 * can disagree with its date (seen on an Echo Mini).
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

#define ms_now         FN(0x03051638, u32 (*)(void))
#define rtc_read       FN(0x0305DA2C, int (*)(u8 *regs))
#define rtc_decode     FN(0x0305DAC4, int (*)(const u8 *regs, void *t))
#define current_screen FN(0x0305C41C, const volatile u32 *(*)(void))
#define request_redraw FN(0x0305C51C, void (*)(const void *screen))

#define RESYNC_MS   60000u
#define CURRENT_MODULE (*(volatile u32 *)lit(0x030399C8u))   /* 48 = Settings (Clock lives there) */

struct rtc_time { u16 year; u8 month, day, hour, min, sec, wday; };

#define STATE __attribute__((section(".text.clock_state,\"ax\",%progbits@"), used))
STATE volatile u32 band_sync_ms = 0;
STATE volatile u32 band_sync_sec = 0;
STATE volatile u32 band_sync_ok = 0;
STATE volatile u32 band_ymd = 0;
STATE volatile u32 band_wday = 0;
STATE volatile u32 band_need_sync = 1;
STATE volatile u32 band_shown_min = 0xFFFFFFFFu;
STATE volatile u32 band_last_module = 0;

/* 0 = Sunday (Sakamoto's method; the Gregorian calendar, any year the chip can hold) */
static u32 weekday(u32 y, u32 m, u32 d)
{
    static const u8 T[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3u)
        y -= 1u;
    return (y + y / 4u - y / 100u + y / 400u + T[(m - 1u) % 12u] + d) % 7u;
}

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
    band_wday = (t.year >= 2000u && t.year < 2100u) ? weekday(t.year, t.month, t.day) : t.wday % 7u;
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

void clock_tick(void)
{
    u32 now = ms_now();
    u32 m = CURRENT_MODULE;
    if (band_last_module == 48u && m != 48u)
        band_need_sync = 1;                   /* left Settings: the clock may have been set */
    band_last_module = m;
    if (band_need_sync || now - band_sync_ms >= RESYNC_MS)
        sync_clock(now);
    u32 mins = minutes_now(now);
    if (mins != band_shown_min) {
        band_shown_min = mins;
        const volatile u32 *screen = current_screen();
        if (screen)
            request_redraw((const void *)screen);
    }
}

void clock_invalidate(void)
{
    band_need_sync = 1;
}
