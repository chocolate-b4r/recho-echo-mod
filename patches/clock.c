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
 *
 * Setting the clock from a computer: TIMESET.TXT at the top of the internal drive, one line
 * "YYYY-MM-DD HH:MM:SS". It is read at start-up and when USB disk is left with Back
 * (usb_key_exit), written to the chip with RECHO's own routine (0x305DB60, the one Settings ->
 * Clock uses), and its first character then becomes '#' so it is used once.
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
#define rtc_write      FN(0x0305DB60, int (*)(const void *t))
#define volume_of      FN(0x0305A02C, void *(*)(u32 vol))
#define find83         FN(0x030531F0, int (*)(void *volume, const char *name83, void *file))
#define file_read      FN(0x03053934, int (*)(void *volume, void *file, u32 offset, void *buf, u32 len))
#define file_write     FN(0x03053BF4, int (*)(void *volume, const char *name83, const void *buf, u32 len))

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
STATE volatile u32 timeset_pending = 1;       /* start-up; keys.c sets it again after USB disk */

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

static u32 digits(const char *s, u32 n)
{
    u32 v = 0;
    for (u32 i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0xFFFFFFFFu;
        v = v * 10u + (u32)(s[i] - '0');
    }
    return v;
}

/* TIMESET.TXT -> the clock chip (see the top of this file) */
__attribute__((noinline)) static void timeset(void)
{
    void *v = volume_of(0);
    if (!v)
        return;                               /* drive not mounted yet: try again */
    timeset_pending = 0;
    u32 file[12];
    char b[20];
    if (find83(v, "TIMESET TXT", file) != 0 || file_read(v, file, 0, b, 19u) != 19 || b[0] == '#')
        return;
    if (b[4] != '-' || b[7] != '-' || b[10] != ' ' || b[13] != ':' || b[16] != ':')
        return;
    u32 y = digits(b, 4), mo = digits(b + 5, 2), d = digits(b + 8, 2);
    u32 h = digits(b + 11, 2), mi = digits(b + 14, 2), s = digits(b + 17, 2);
    if (y < 2000u || y > 2099u || mo < 1u || mo > 12u || d < 1u || d > 31u || h > 23u || mi > 59u || s > 59u)
        return;
    struct rtc_time t = {(u16)y, (u8)mo, (u8)d, (u8)h, (u8)mi, (u8)s, (u8)weekday(y, mo, d)};
    if (rtc_write(&t) != 0)
        return;
    b[0] = '#';                               /* used: not again at the next start */
    file_write(v, "TIMESET TXT", b, 19u);
    band_need_sync = 1;
}

void clock_tick(void)
{
    u32 now = ms_now();
    if (timeset_pending)
        timeset();
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
