/*
 * Screens drawn by the mod (both players; Echo-only parts under ECHO_PORT).
 *
 *   np_draw      Now Playing, replaces module m45's draw (b.w at 0x307A1F0). Draws RECHO's
 *                320 x 170 frame buffer: top bar (time, date, queue position), "Artist - Song",
 *                album, a large play-state glyph (red PAUSED when paused, volume for 2 s after
 *                a change), progress bar (red when paused), and one format line.
 *   usb_draw     cable chooser (USBSEL) draw, replaces the pointer at 0x3060AD4: two halves,
 *                Charge only / USB disk, the selected one outlined and bright.
 *   hints_stub   HINTS layer draw (0x3060BBC). No button labels any more. Echo: notes that
 *                the message line (echo_band.c) should update. Mini: draws RECHO's own bar only
 *                while it has a message, over the bottom rows, and redraws the screen after.
 *
 * Data, all RECHO's own (read only):
 *   0x3057BD0 player state (1 playing, 2 paused)    0x3057BE4 elapsed ms
 *   0x3057BDC format: [0] rate Hz, [2] bits, [3] kbps, [4..5] length in samples
 *   0x305A194 queue loaded   0x305A1B0 queue index   0x305A1BC queue info ([1] count)
 *   0x305A8F8(index) tag record or 0 (+0 title, +160 artist, +256 album)
 *   0x305A1CC file name, 0x305A1C4 folder name   0x3057DB4/0x3057DC0 ReplayGain mode / gain (cdB)
 *   0x030041D0 volume dB, 0x0303AB8C time of the last volume change
 *   chooser: cursor 0x030041B4, countdown flag 0x030399B8, countdown start 0x030399BC
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
#define PTR(type, a)   ((type)lit(a))

#define draw_text    FN(0x03058D84, int (*)(int font, int x, int y, const char *s, int maxw, u32 fg, int bg))
#define text_width   FN(0x03058D20, int (*)(int font, const char *s))
#define fill_rect    FN(0x0306BF2C, void (*)(int, int, int, int, u32))
#define ms_now       FN(0x03051638, u32 (*)(void))
#define fmt_time     FN(0x03059AE8, void (*)(char *buf, u32 ms))
#define gpio_read    FN(0x03051610, u32 (*)(u32 bank, u32 pin))
#define player_state FN(0x03057BD0, int (*)(void))
#define elapsed_ms   FN(0x03057BE4, u32 (*)(void))
#define fmt_info     FN(0x03057BDC, const volatile u32 *(*)(void))
#define queue_loaded FN(0x0305A194, int (*)(void))
#define queue_index  FN(0x0305A1B0, int (*)(void))
#define queue_info   FN(0x0305A1BC, const volatile int *(*)(void))
#define tag_record   FN(0x0305A8F8, const char *(*)(int index))
#define file_name    FN(0x0305A1CC, const char *(*)(void))
#define folder_name  FN(0x0305A1C4, const char *(*)(void))

#define FONTS        PTR(volatile int *, 0x03034B48)
#define VOLUME_DB    (*PTR(volatile int *, 0x030041D0))
#define VOLUME_AT    REG(0x0303AB8C)
#define USB_CURSOR   REG(0x030041B4)
#define USB_COUNTING REG(0x030399B8)
#define USB_START    REG(0x030399BC)
#define BATTERY      PTR(volatile int *, 0x030401EC)
#define PLAY_MODE    (PTR(volatile unsigned char *, 0x0303EDD0)[7])   /* settings block, byte 7 */

#define W 320
#define SEP " \xee\x80\xa1 "      /* RECHO's own separator dot glyph (U+E021) */
#ifdef ECHO_PORT
#define FSEP SEP
#define F_GAP "  "
#define F_KHZ " kHz"
#define F_KBPS " kbps"
#define F_BIT " bit"
#else                                   /* Mini: "MP3 44.1kHz·320kbps" fits the 146 px column */
#define FSEP "\xee\x80\xa1"
#define F_GAP " "
#define F_KHZ "kHz"
#define F_KBPS "kbps"
#define F_BIT "bit"
#endif
#define BLACK 0x0000u
#define WHITE 0xF79Eu
#define SOFT  0xBDD7u
#define GREY  0x8410u
#define DIM   0x52AAu
#define RULE  0x4208u
#define RED   0xE249u
#define DKRED 0x5082u
#define GREEN 0x47E8u
#define AMBER 0xFD20u

#define STATE __attribute__((section(".text.np_state,\"ax\",%progbits@"), used))
STATE volatile u32 hints_dirty = 1;
STATE char title_buf[96] = {0};
STATE char album_buf[96] = {0};

extern void clock_strings(char *time, char *date);  /* clock.c */

#define current_screen FN(0x0305C41C, const volatile u32 *(*)(void))
#define request_redraw FN(0x0305C51C, void (*)(const void *screen))
#define bar_message    FN(0x0305AE28, const char *(*)(void))
#define hints_draw     FN(0x0305AEEC, void (*)(void))
STATE volatile u32 msg_shown = 0;

/* Output in use, from RECHO's own jack detection 0x3055EC0 (bit 0 = 3.5 mm plugged,
 * GPIO0.13; bit 1 = 4.4 mm plugged, GPIO2.8, already corrected for the Echo by
 * echo_gpio_read). Returns 0 when nothing is plugged in, so nothing is shown.
 * bt_routed (set once Bluetooth audio is in) shows "BT" instead. */
#define jacks FN(0x03055EC0, u32 (*)(void))
extern volatile u32 bt_state;            /* bt.c */
extern char bt_conn_name[];
#define BT_CONNECTED 6u
#ifdef ECHO_PORT
extern volatile u32 lyrics_track_size;   /* lyrics.c: size of the playing file, 0 = unknown */
#endif
STATE volatile u32 bt_routed = 0;

static const char *output_name(void)
{
    if (bt_routed)
        return "BT";
    u32 j = jacks();
    if (j & 2u)
#ifdef ECHO_PORT
        return "4.4 mm";
#else
        return "4.4mm";
#endif
    if (j & 1u)
#ifdef ECHO_PORT
        return "3.5 mm";
#else
        return "3.5mm";
#endif
    return 0;
}

#ifndef ECHO_PORT
static char *put_pct(char *p, char *end);
static u32 pct_colour(void);
STATE char pct_buf[8] = {0};

/* Mini menu bar: battery as "88%" instead of RECHO's level glyph (patched at 0x3059474) */
const char *status_pct(void)
{
    char *p = put_pct(pct_buf, pct_buf + 7);
    *p = 0;
    return pct_buf;
}

/* `ldr.w r5,[r3,r2,lsl #2]` -> bl here: r5 = the text, everything else kept */
__attribute__((naked)) void battery_pct_shim(void)
{
    __asm__ volatile(
        "push   {r0, r1, r2, r3, ip, lr}\n"
        "bl     status_pct\n"
        "mov    r5, r0\n"
        "pop    {r0, r1, r2, r3, ip, pc}\n");
}

/* RECHO's menu bar draws that text in white (`movw r2,#0xF79E` at 0x3059490); this bl gives it the
 * Now Playing colours instead: green on the cable, amber <= 35 %, red <= 15 %. r2 = colour. */
u32 status_pct_colour(void)
{
    return pct_colour();
}

__attribute__((naked)) void battery_colour_shim(void)
{
    __asm__ volatile(
        "push   {r0, r1, r3, ip, lr}\n"
        "bl     status_pct_colour\n"
        "mov    r2, r0\n"
        "pop    {r0, r1, r3, ip, pc}\n");
}
#endif

/* ---- helpers ------------------------------------------------------------------ */

static int font_h(int font)
{
    int h = FONTS[font * 4 + 2];
    return (h > 0 && h < 40) ? h : 12;
}

/* text with its vertical centre on y_mid; returns the width drawn */
int text_at(int font, int x, int y_mid, const char *s, int maxw, u32 fg)   /* also used by bt.c */
{
    if (!s || !*s || !FONTS[font * 4 + 3])
        return 0;
    int w = text_width(font, s);
    if (w > maxw)
        w = maxw;
    draw_text(font, x, y_mid - font_h(font) / 2, s, maxw, fg, -1);
    return w;
}

static int text_centred(int font, int cx, int y_mid, const char *s, int maxw, u32 fg)
{
    if (!s || !*s)
        return 0;
    int w = text_width(font, s);
    if (w > maxw)
        w = maxw;
    return text_at(font, cx - w / 2, y_mid, s, maxw, fg);
}

static char *put_str(char *p, const char *s, char *end)
{
    while (s && *s && p < end)
        *p++ = *s++;
    return p;
}

static char *put_num(char *p, u32 v, char *end)
{
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && n < 10);
    while (n && p < end)
        *p++ = tmp[--n];
    return p;
}

/* ---- position in the play order ----------------------------------------------------
 * RECHO's queue index is the file order. With shuffle, the songs play in a fixed scrambled
 * order per pass; its "next" routine (0x3065FE4, also used by Up Next) walks it. Songs left
 * to the end of the pass = n - position, so position = n - remaining. Infinite shuffle is
 * walked as plain shuffle (the current pass). Cached until the track or the queue changes. */
#define queue_next_entry FN(0x03065FE4, int (*)(volatile void *q, u32 cur, u32 mode, u32 flag))
#define QUEUE_OBJ        PTR(volatile void *, 0x0303AB90)
#define QUEUE_SEED       REG(0x0303AB98)
#define QUEUE_PASS       REG(0x0303ABA8)
#define QUEUE_ACTIVE     REG(0x0303BD74)
#define QUEUE_CURRENT    REG(0x0303BD78)
STATE volatile u32 qp_key[5] = {0xFFFFFFFFu, 0, 0, 0, 0};
STATE volatile u32 qp_pos = 0;

static u32 play_position(u32 *count)
{
    const volatile int *qi = queue_info();
    u32 n = (qi && qi[1] > 0) ? (u32)qi[1] : 0u;
    *count = n;
    if (!n)
        return 0;
    u32 cur = QUEUE_ACTIVE ? QUEUE_CURRENT : (u32)queue_index();
    u32 mode = PLAY_MODE;
    if (mode != 1u && mode != 4u)
        return cur + 1u;                        /* in order: the file order is the play order */
    u32 key[5] = {cur, QUEUE_SEED, QUEUE_PASS, n, mode};
    int same = 1;
    for (int i = 0; i < 5; i++)
        if (key[i] != qp_key[i])
            same = 0;
    if (same)
        return qp_pos;
    for (int i = 0; i < 5; i++)
        qp_key[i] = key[i];
    u32 c = cur, left = 0;
    for (u32 i = 0; i < n; i++) {
        int nx = queue_next_entry(QUEUE_OBJ, c, 1u, 0);
        if (nx < 0 || (u32)nx == c || (u32)nx >= n)
            break;
        c = (u32)nx;
        left++;
    }
    qp_pos = left < n ? n - left : cur + 1u;    /* no end found: fall back to the file order */
    return qp_pos;
}

/* "3/500" */
static char *put_qpos(char *p, char *end)
{
    u32 n, pos = play_position(&n);
    if (!n)
        return p;
    p = put_num(p, pos, end);
    p = put_str(p, "/", end);
    return put_num(p, n, end);
}


/* ---- RECHO's screen header (0x3059B10: Files, Settings, ...) ---------------------------
 * The header's icon (`mov r0,r7; bl 0x3059338` at 0x3059B8C) is replaced by time | date,
 * drawn here at the header's row (r5); the title then starts where the date ends
 * (`movs r1,#40` at 0x3059B92 -> `mov r1,r0`). */
int hdr_clock(int y_top)
{
    char t[8], d[16];
    clock_strings(t, d);
    int ym = y_top + font_h(1) / 2;
    int x = 8 + text_at(1, 8, ym, t, 60, WHITE);
    fill_rect(x + 5, y_top + 3, x + 5, y_top + 15, GREY);
    x += 11;
    x += text_at(1, x, ym, d, 120, SOFT);
    return x + 10;
}

/* ---- top bar shared by Now Playing and the chooser --------------------------- */

static void top_bar_c(const char *right, u32 right_colour);

static void top_bar(const char *right)
{
    top_bar_c(right, SOFT);
}

#ifdef ECHO_PORT
#define VOP_ON REG(0x030089EC)              /* RECHO: panel on the VOP bus (sidebar and strip need it) */
#endif
static void top_bar_3(const char *pre, const char *right, u32 right_colour, const char *last, u32 last_colour);
static void top_bar_2(const char *right, u32 right_colour, const char *last, u32 last_colour)
{
    top_bar_3(0, right, right_colour, last, last_colour);
}

/* time | date on the left; on the right "pre" (grey, dropped when it would reach the date),
 * "right", then "last" at the edge */
static void top_bar_3(const char *pre, const char *right, u32 right_colour, const char *last, u32 last_colour)
{
#ifdef ECHO_PORT
    if (!VOP_ON) {                          /* slow display fallback: sidebar / strip cannot draw */
        right = "Safe display: restart";
        right_colour = AMBER;
        last = 0;
    }
#endif
    char t[8], d[16];
    clock_strings(t, d);
    int x = 8 + text_at(1, 8, 10, t, 60, WHITE);
    fill_rect(x + 5, 4, x + 5, 16, GREY);                 /* "21:47 | Fri 13 Nov" */
    int xd = x + 11 + text_at(1, x + 11, 10, d, 120, SOFT);
    int xr = W - 8;
    if (last && *last) {
        int w = text_width(1, last);
        xr -= w;
        text_at(1, xr, 10, last, 60, last_colour);
        xr -= 6;
    }
    if (right && *right) {
        int w = text_width(1, right);
        if (w > 140)
            w = 140;
        text_at(1, xr - w, 10, right, 140, right_colour);
        xr -= w + 10;
    }
    if (pre && *pre) {
        int w = text_width(1, pre);
        if (xr - w >= xd + 10)
            text_at(1, xr - w, 10, pre, w + 1, GREY);
    }
    fill_rect(0, 21, W - 1, 21, RULE);
}

static void top_bar_c(const char *right, u32 right_colour)
{
    top_bar_2(right, right_colour, 0, 0);
}

/* ---- Now Playing ------------------------------------------------------------- */

static const char *without_ext(const char *s, char *buf, int n)
{
    char *p = buf, *end = buf + n - 1, *dot = 0;
    for (const char *q = s; q && *q && p < end; q++, p++) {
        *p = *q;
        if (*q == '.')
            dot = p;
        if (*q == '/')
            dot = 0;
    }
    *p = 0;
    if (dot && dot != buf && p - dot <= 5)
        *dot = 0;
    return buf;
}

/* file type, rate, bits (or kbps), then with qpos the queue position ("3/500") */
static void format_line_ex(char *out, int n, int qpos)
{
    char *p = out, *end = out + n - 1;
    const char *name = file_name();
    const char *dot = 0;
    for (const char *q = name; q && *q; q++)
        if (*q == '.')
            dot = q + 1;
    if (dot && *dot) {
        for (const char *q = dot; *q && q - dot < 4 && p < end; q++)
            *p++ = (*q >= 'a' && *q <= 'z') ? (char)(*q - 32) : *q;
        p = put_str(p, F_GAP, end);
    }
    const volatile u32 *f = fmt_info();
    if (f && f[0]) {
        u32 r = f[0];
        p = put_num(p, r / 1000u, end);
        if (r % 1000u >= 100u) {
            *p++ = '.';
            p = put_num(p, (r % 1000u) / 100u, end);
        }
        p = put_str(p, F_KHZ, end);
        if (f[3]) {
            p = put_str(p, FSEP, end);
            p = put_num(p, f[3], end);
            p = put_str(p, F_KBPS, end);
        } else if (f[2]) {
            p = put_str(p, FSEP, end);
            p = put_num(p, f[2], end);
            p = put_str(p, F_BIT, end);
#ifdef ECHO_PORT
            /* lossless: average bitrate from the file size and the length */
            u32 lo = f[4], dur = (lo / r) * 1000u + ((lo % r) * 1000u) / r;
            u32 size = lyrics_track_size;
            if (size && dur >= 1000u) {
                u32 kbps = (size / dur) * 8u + ((size % dur) * 8u) / dur;
                p = put_str(p, SEP, end);
                p = put_num(p, kbps, end);
                p = put_str(p, " kbps", end);
            }
#endif
        }
    }
    if (qpos) {
        u32 qn;
        play_position(&qn);
        if (qn) {
            if (p != out)
                p = put_str(p, SEP, end);
            p = put_qpos(p, end);
        }
    }
    *p = 0;
}

/* ---- pieces shared by Now Playing and the Echo's player strip ---------------------- */

#ifdef ECHO_PORT                          /* draft C: song, album, rule, format line; strip below */
#define Y_TITLE    46
#define Y_ALBUM    72
#define Y_RULE     91
#define Y_INFO     105
#define Y_GLYPH    134
#else
/* Mini: title, album, next song, format line, then the progress bar on the same row as in
 * the menu (158), so it does not jump between the two screens */
#define Y_TITLE    44
#define Y_ALBUM    68
#define Y_NEXT     100
#define Y_GLYPH    Y_NEXT                   /* Echo-only code below the Mini's return */
#define Y_FORMAT   132
#define Y_PROGRESS 158
#endif

static const char *track_title(const char **artist)
{
    const char *tags = tag_record(queue_index());
    if (tags && tags[0]) {
        *artist = tags[160] ? tags + 160 : 0;
        return tags;
    }
    *artist = 0;
    return without_ext(file_name(), title_buf, sizeof title_buf);
}

#ifdef ECHO_PORT                          /* the Mini's Now Playing has no album line */
static const char *track_album(void)
{
    const char *tags = tag_record(queue_index());
    if (tags && tags[0])
        return tags[256] ? tags + 256 : 0;
    return without_ext(folder_name(), album_buf, sizeof album_buf);
}
#endif

#ifdef ECHO_PORT
/* "Artist - Song" from x (or centred when x < 0), artist light grey, song white */
static void track_line_xy(int x_or_centre, int y, int paused)
{
    const char *artist;
    const char *title = track_title(&artist);
    u32 c_title = paused ? GREY : WHITE, c_art = paused ? DIM : SOFT;
    int room = x_or_centre < 0 ? 300 : W - 8 - x_or_centre;
    int wt = text_width(1, title), wa = artist ? text_width(1, artist) : 0, ws = text_width(1, " - ");
    if (artist && wa + ws + wt <= room) {
        int x = x_or_centre < 0 ? (W - (wa + ws + wt)) / 2 : x_or_centre;
        x += text_at(1, x, y, artist, wa + 1, c_art);
        x += text_at(1, x, y, " - ", ws + 1, GREY);
        text_at(1, x, y, title, wt + 1, c_title);
    } else if (x_or_centre < 0) {
        text_centred(1, W / 2, y, title, 300, c_title);
    } else {
        text_at(1, x_or_centre, y, title, room, c_title);
    }
}

static void track_line(int y, int paused)
{
    track_line_xy(-1, y, paused);
}
#endif

static void progress_at(int y, int paused)
{
    const volatile u32 *f = fmt_info();
    u32 el = elapsed_ms(), dur = 0;
    if (f && f[0]) {
        u32 lo = f[4], rate = f[0];
        dur = (lo / rate) * 1000u + ((lo % rate) * 1000u) / rate;
    }
    char te[12], td[12];
    fmt_time(te, el);
    fmt_time(td, dur);
    text_at(0, 10, y, te, 40, paused ? RED : WHITE);
    int wd = text_width(0, td);
    text_at(0, W - 10 - wd, y, td, 40, paused ? DIM : WHITE);
    const int bx0 = 50, bx1 = W - 51;
    u32 fillx = bx0;
    if (dur && el < dur)
        fillx = bx0 + (u32)(bx1 - bx0) * (el / 100u) / (dur / 100u ? dur / 100u : 1u);
    else if (dur)
        fillx = bx1;
    fill_rect(bx0, y - 2, bx1, y + 2, paused ? DKRED : RULE);
    if ((int)fillx > bx0)
        fill_rect(bx0, y - 2, (int)fillx, y + 2, paused ? RED : WHITE);
}

#ifndef ECHO_PORT
/* Format line from x, y in the large font, wrapped at the separator dots so each line fits w
 * (like the artist / song / album lines above it); stops before y_max. Returns the next y. */
static int format_left(int x, int y, int w, int y_max, int paused)
{
    char line[96], cur[96];
    format_line_ex(line, sizeof line, 0);
    u32 c = paused ? DIM : GREY;
    const int sl = sizeof FSEP - 1;
    int n = 0;                                   /* chars in cur */
    const char *seg = line;
    while (*seg && y <= y_max) {
        const char *e = seg;                     /* end of this segment: next SEP or the end */
        while (*e) {
            int k = 0;
            while (k < sl && e[k] == FSEP[k])
                k++;
            if (k == sl)
                break;
            e++;
        }
        char tryb[96];
        int t = 0;
        for (int i = 0; i < n && t < 95; i++)
            tryb[t++] = cur[i];
        if (n)
            for (int i = 0; i < sl && t < 95; i++)
                tryb[t++] = FSEP[i];
        for (const char *q = seg; q < e && t < 95; q++)
            tryb[t++] = *q;
        tryb[t] = 0;
        if (n && text_width(1, tryb) > w) {      /* flush the line, start a new one with this segment */
            cur[n] = 0;
            text_at(1, x, y, cur, w, c);
            y += 18;
            n = 0;
            for (const char *q = seg; q < e && n < 95; q++)
                cur[n++] = *q;
        } else {
            for (n = 0; n < t; n++)
                cur[n] = tryb[n];
        }
        seg = *e ? e + sl : e;
    }
    if (n && y <= y_max) {
        cur[n] = 0;
        text_at(1, x, y, cur, w, c);
        y += 18;
    }
    return y;
}
#endif

static const char *output_label(char *buf, int n, u32 *colour);

/* favs.c: the song playing is in FAVORITE.M3U; a 9 x 8 heart after its title */
extern volatile u32 fav_is_current;
static void heart(int x, int y_top)
{
    static const char *const ROWS[8] = {".##...##.", "####.####", "#########", "#########",
                                        ".#######.", "..#####..", "...###...", "....#...."};
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 9; c++)
            if (ROWS[r][c] == '#')
                fill_rect(x + c, y_top + r, x + c, y_top + r, RED);
}
extern const char *lyrics_line(void);
#define FB_PX PTR(volatile u16 *, 0x03008A00)
/* draw "a" then "b" (either may be 0) in the large font scaled, centred on cx (or from -cx when
 * cx < 0), top at y_top. num / 2 = scale; num 2 = 1x in 2 px strokes, as 1.5x draws them */
static int text_scaled_line(int cx, int y_top, const char *a, u32 ca, const char *b, u32 cb, int num)
{
    /* num / 2 = scale: 4 -> 2x, 3 -> 1.5x. Rendered at 1x into scratch rows 0..h of the frame
     * buffer (the top bar's rows: np_draw draws the bar after the picture), copied down, then
     * cleared. 0 = does not fit 300 px. */
    const int sy = 0;
    int h = font_h(1);
    int wa = (a && *a) ? text_width(1, a) : 0, ws = wa ? text_width(1, " - ") : 0;
    int wb = (b && *b) ? text_width(1, b) : 0, w = wa + ws + wb;
    if (!w || w * num > 600)
        return 0;
    fill_rect(0, sy, w + 1, sy + h, BLACK);
    int x = 0;
    if (wa) {
        draw_text(1, x, sy, a, wa + 1, ca, -1);
        x += wa;
        draw_text(1, x, sy, " - ", ws + 1, GREY, -1);
        x += ws;
    }
    if (wb)
        draw_text(1, x, sy, b, wb + 1, cb, -1);
    int dw = w * num / 2, dh = h * num / 2, x0 = cx >= 0 ? cx - dw / 2 : -cx;
    if (num <= 3) {
        /* 1.5x: each lit source pixel becomes a 2 x 2 block at its 1.5x place, so every stroke is
         * 2 px wide (copying by nearest pixel made one side of a letter 1 px and the other 2 px) */
        for (int r = 0; r < h; r++) {
            const volatile u16 *src = FB_PX + (sy + r) * W;
            for (int c = 0; c < w; c++) {
                u16 px = src[c];
                if (!px)
                    continue;
                volatile u16 *dst = FB_PX + (y_top + r * num / 2) * W + x0 + c * num / 2;
                dst[0] = px;
                dst[1] = px;
                dst[W] = px;
                dst[W + 1] = px;
            }
        }
    } else {
        for (int r = 0; r < dh; r++) {
            const volatile u16 *src = FB_PX + (sy + r * 2 / num) * W;
            volatile u16 *dst = FB_PX + (y_top + r) * W + x0;
            for (int c = 0; c < dw; c++)
                dst[c] = src[c * 2 / num];
        }
    }
    fill_rect(0, sy, w + 1, sy + h, BLACK);
    return 1;
}
#define text2x_line(cx, y, a, ca, b, cb) text_scaled_line(cx, y, a, ca, b, cb, 4)

/* The lyric being sung, always at 1.5x (rows 118..169). A line too wide for one row is split at
 * the space that balances the two halves best; only lines too long even for two rows of 1.5x
 * fall back to two rows of the large font (then the small one). */
#ifdef ECHO_PORT                          /* the Mini draws its lyric with big_line */
static void lyric_block(const char *ly, u32 c, int dy)
{
    char a[128], b[128];
    int n = 0;
    while (ly[n] && n < 127)
        n++;
    if (text_width(1, ly) <= 200) {
        text_scaled_line(W / 2, 137 + dy, 0, 0, ly, c, 3);
        return;
    }
    int best = -1, best_w = 1 << 30;
    for (int i = 1; i < n - 1; i++) {
        if (ly[i] != ' ')
            continue;
        for (int k = 0; k < i; k++)
            a[k] = ly[k];
        a[i] = 0;
        int wa = text_width(1, a), wb = text_width(1, ly + i + 1);
        int m = wa > wb ? wa : wb;
        if (m < best_w) {
            best_w = m;
            best = i;
        }
    }
    if (best < 0) {                       /* one long word: the large font, clipped */
        text_centred(text_width(1, ly) <= 304 ? 1 : 0, W / 2, 148 + dy, ly, 304, c);
        return;
    }
    for (int k = 0; k < best; k++)
        a[k] = ly[k];
    a[best] = 0;
    int j = 0;
    for (const char *q = ly + best + 1; *q && j < 127; q++)
        b[j++] = *q;
    b[j] = 0;
    if (best_w <= 200) {
        text_scaled_line(W / 2, 124 + dy, 0, 0, a, c, 3);  /* Echo: the second row ends at */
        text_scaled_line(W / 2, 146 + dy, 0, 0, b, c, 3);  /* the picture's last row (169) */
    } else {
        int f = best_w <= 304 ? 1 : 0;
        text_centred(f, W / 2, 138 + dy, a, 304, c);
        text_centred(f, W / 2, 158 + dy, b, 304, c);
    }
}
#endif

#ifndef ECHO_PORT
/* How many bytes of s fit in w at 1x in the large font: up to the last space that fits, else
 * (one long word) up to the last letter that fits; at least one letter */
static int fit_words(const char *s, int w)
{
    char buf[128];
    int n = 0, sp = -1, ok = 0;
    while (s[n] && n < 127) {
        buf[n] = s[n];
        buf[n + 1] = 0;
        if (((unsigned char)s[n + 1] & 0xC0u) != 0x80u) {   /* at a letter's end (UTF-8) */
            if (text_width(1, buf) > w)
                break;
            ok = n + 1;
            if (s[n + 1] == ' ' || !s[n + 1])
                sp = n + 1;
        }
        n++;
    }
    if (!s[ok])
        return ok;
    return sp > 0 ? sp : (ok ? ok : 1);
}

/* The lyric in the left column: the large font, as many words per line as fit, the rest on the
 * next lines (a word too long for a line is broken between letters); y = middle of the first
 * line, lines 18 apart, none past y_max. */
static void lyric_left(int x, int y, int w, int y_max, const char *s, u32 c)
{
    char line[128];
    while (s && *s && y <= y_max) {
        int k = fit_words(s, w);
        for (int i = 0; i < k && i < 127; i++)
            line[i] = s[i];
        line[k < 127 ? k : 127] = 0;
        text_centred(1, x + w / 2, y, line, w + 8, c);   /* centred in the column */
        s += k;
        while (*s == ' ')
            s++;
        y += 18;
    }
}

/* The song title from x at y_top: the large font at 1.5x in 2 px strokes; what does not fit a line
 * carries on to the next, up to three lines (only the third is cut with ".." if still too long).
 * Returns the next free row; *last_w = the last line's width (for the heart), *last_y = its top. */
static int title_lines(int x, int y_top, const char *s, u32 c, int w, int *last_w, int *last_y)
{
    char a[128];
    const int lh = 24, sw = w * 2 / 3;            /* width at 1x that fills w at 1.5x */
    int y = y_top;
    for (int line = 0; s && *s && line < 3; line++) {
        int k = fit_words(s, sw);
        if (line == 2 && s[k]) {                  /* third line and more left: cut it here */
            int n = k > 2 ? k - 2 : 0;
            while (n > 0 && ((unsigned char)s[n] & 0xC0u) == 0x80u)
                n--;
            for (int i = 0; i < n; i++)
                a[i] = s[i];
            a[n] = '.';
            a[n + 1] = '.';
            a[n + 2] = 0;
            while (n > 0 && text_width(1, a) > sw) {
                n--;
                while (n > 0 && ((unsigned char)a[n] & 0xC0u) == 0x80u)
                    n--;
                a[n] = '.';
                a[n + 1] = '.';
                a[n + 2] = 0;
            }
            k = 1 << 20;
        } else {
            for (int i = 0; i < k && i < 127; i++)
                a[i] = s[i];
            a[k < 127 ? k : 127] = 0;
        }
        text_scaled_line(-x, y, 0, 0, a, c, 3);
        *last_w = text_width(1, a) * 3 / 2;
        *last_y = y;
        y += lh;
        if (k >= (1 << 20))
            break;
        s += k;
        while (*s == ' ')
            s++;
    }
    return y;
}

/* ---- Mini Up Next (right half of Now Playing) ----------------------------------------
 * The upcoming entries in play order (RECHO's own "next", 0x3065FE4), named from the queue
 * store as the Echo's Up Next does (0x3064B64: one 36-byte index read per entry). Read one
 * entry per main-loop pass by mini_upnext_tick, only on Now Playing and only after the queue
 * or the track changed (300 ms settle); np_draw only draws the cached names. */
#define queue_name  FN(0x03064B64, int (*)(void *store, u32 entry, char *buf, unsigned char *vol, u32 flag))
#define QSTORE      PTR(void *, 0x0303BD80)
#define UPN_BUF     PTR(char *, 0x03048200)        /* 276 B name buffer (free RAM, as on the Echo) */
#define UPN_CACHE   PTR(char *, 0x03044000)        /* UPN_N x UPN_LEN names (free RAM) */
#define UPN_N       9
#define UPN_LEN     64
#define UPN_SETTLE  300u
STATE volatile u32 upn_key[6] = {0xFFFFFFFFu, 0, 0, 0, 0, 0};
STATE volatile u32 upn_have = 0, upn_cursor = 0, upn_walk = 0, upn_at = 0, upn_end = 0;

static void upn_redraw(void)
{
    const volatile u32 *scr = current_screen();
    if (scr)
        request_redraw((const void *)scr);
}

STATE volatile int lyr_shown = -2;

void mini_upnext_tick(void)
{
    if (REG(0x030399C8) != 45u)              /* Now Playing only */
        return;
    if (REG(0x03006174)) {                   /* screen on: the .lrc and the line being sung */
        extern void lyrics_tick(void);
        extern int lyrics_index(void);
        lyrics_tick();
        int li = lyrics_index();
        if (li != lyr_shown) {
            lyr_shown = li;
            upn_redraw();
        }
    }
    const volatile int *qi = queue_info();
    u32 n = (qi && qi[1] > 0) ? (u32)qi[1] : 0u;
    u32 key[6] = {QUEUE_ACTIVE, QUEUE_CURRENT, PLAY_MODE, QUEUE_SEED, QUEUE_PASS, n};
    u32 now = ms_now();
    int changed = 0;
    for (int i = 0; i < 6; i++)
        if (key[i] != upn_key[i])
            changed = 1;
    if (changed) {
        for (int i = 0; i < 6; i++)
            upn_key[i] = key[i];
        upn_have = 0;
        upn_end = 0;
        upn_cursor = key[1];
        upn_walk = key[0] && n > 1u;
        upn_at = now;
        return;
    }
    if (!upn_walk || upn_have >= UPN_N || now - upn_at < UPN_SETTLE)
        return;
    int nx = queue_next_entry(QUEUE_OBJ, upn_cursor, PLAY_MODE, 0);
    if (nx < 0 || (u32)nx == upn_cursor || (u32)nx >= n) {
        upn_walk = 0;
        upn_end = 1;
        upn_redraw();
        return;
    }
    unsigned char vol = 0;
    char *buf = UPN_BUF;
    buf[0] = 0;
    if (queue_name(QSTORE, (u32)nx, buf, &vol, 0) != 0) {
        upn_walk = 0;                        /* lookup failed: stop, never retry here */
        upn_redraw();
        return;
    }
    buf[275] = 0;
    char *dot = 0, *q;
    for (q = buf; *q; q++)
        if (*q == '.')
            dot = q;
    if (dot && dot != buf && q - dot <= 5)
        *dot = 0;
    char *dst = UPN_CACHE + upn_have * UPN_LEN;
    int k = 0;
    while (buf[k] && k < UPN_LEN - 1) {
        dst[k] = buf[k];
        k++;
    }
    dst[k] = 0;
    upn_have++;
    upn_cursor = (u32)nx;
    if (upn_have == 1u || upn_have >= UPN_N)  /* first name at once, the rest in one redraw */
        upn_redraw();
}

static void upnext_draw(int x, int y0, int w, int paused, u32 rows)
{
    if (!upn_have) {
        const char *t = upn_end ? "End of queue" : (QUEUE_ACTIVE ? "..." : 0);
        if (t)
            text_at(0, x, y0, t, w, DIM);
        return;
    }
    for (u32 i = 0; i < upn_have && i < rows; i++) {
        u32 c = i == 0 ? (paused ? GREY : 0xE71Cu) : (paused ? 0x52AAu : 0x6B4Du);
        text_at(0, x, y0 + (int)i * 13, UPN_CACHE + i * UPN_LEN, w, c);
    }
}

#endif

void np_draw(void)
{
    fill_rect(0, 0, W - 1, 169, BLACK);
    int loaded = queue_loaded();
    char qpos[64];
    qpos[0] = 0;
    char *p = qpos;
#ifdef ECHO_PORT
    /* output and volume in the top bar; "BT <headphones>" in green while Bluetooth is connected */
    u32 right_colour = SOFT;
    p = put_str(qpos, output_label(qpos + 32, 32, &right_colour), qpos + 31);
#else
    /* Mini: the queue position in grey, the output ("3.5 mm", the headphones' name in green),
     * then the battery; drawn last (the lyric uses the top rows as scratch) */
    u32 right_colour = SOFT;
    char out[48];
    output_label(out, sizeof out, &right_colour);
    if (loaded)
        p = put_qpos(qpos, qpos + 31);
#endif
    *p = 0;
#ifdef ECHO_PORT
    if (!loaded)
        top_bar_c(qpos, right_colour);    /* with a track: after the picture (scratch rows) */
#else
    char pct[8];
    {
        char *q = put_pct(pct, pct + 7);
        *q = 0;
    }
    if (!loaded) {
        top_bar_3(0, out, right_colour, pct, pct_colour());
        text_centred(1, W / 2, 85, "Nothing playing", 300, SOFT);
        return;
    }
#endif

    if (!loaded) {
        text_centred(1, W / 2, 85, "Nothing playing", 300, SOFT);
        return;
    }

    int state = player_state();
    int paused = state == 2, playing = state == 1;

#ifdef ECHO_PORT
    /* Echo: "Artist - Song" at 2x when it fits, album, file info, a rule, then the lyric line
     * being sung at 2x when it fits (the next one is in the strip below). Paused = dimmer. */
    {
        const char *artist;
        const char *title = track_title(&artist);
        u32 c_title = paused ? GREY : WHITE, c_art = paused ? DIM : SOFT;
        int hx = 6, hy = 48;                  /* heart: after the title, else at the left */
        if (text2x_line(W / 2, 36, artist, c_art, title, c_title)) {
            int w = (artist && *artist) ? text_width(1, artist) + text_width(1, " - ") : 0;
            hx = W / 2 + w + text_width(1, title) + 6;
        } else if (artist && *artist && title && *title && text_width(1, title) <= 150) {
            text_centred(1, W / 2, 34, artist, 300, c_art);         /* artist over a 2x title */
            text2x_line(W / 2, 42, 0, 0, title, c_title);
            hx = W / 2 + text_width(1, title) + 6;
            hy = 54;
        } else {
            track_line(52, paused);
        }
        if (fav_is_current)
            heart(hx > W - 14 ? 6 : hx, hy);
        text_centred(1, W / 2, 82, track_album(), 300, paused ? DIM : GREY);
        char info[64];
        format_line_ex(info, sizeof info, 1);   /* format, then the queue position ("3/500") */
        text_centred(text_width(1, info) <= 300 ? 1 : 0, W / 2, 102, info, 300, paused ? DIM : GREY);
        fill_rect(60, 118, W - 61, 118, RULE);  /* 8 px of room under the file info */

        const char *ly = lyrics_line();       /* the volume is in the top bar: no overlay here */
        if (ly)
            lyric_block(ly, paused ? GREY : WHITE, 0);
    }
    top_bar_c(qpos, right_colour);
    (void)playing;
    return;
#else
    /* Mini: left half the song (one line), the artist, the format line, then the lyric being sung
     * (up to two lines); right half Up Next in the small font (9 songs). Queue position in the
     * top bar. */
    {
        const char *artist;
        const char *title = track_title(&artist);
        u32 c_title = paused ? GREY : WHITE, c_art = paused ? DIM : SOFT;
        const int LX = 8, LW = 146;
        int tw = 0, ty = 28;
        int y = title_lines(LX, 28, title, c_title, LW, &tw, &ty) + 11;
        int xe = LX + tw;                          /* end of the title's last line */
        if (fav_is_current) {
            heart(xe + 5 > 145 ? 145 : xe + 5, ty + 8);
            xe += 14;
        }
        if (artist && *artist) {
            /* the artist on the title's last line when it fits there (bottoms aligned), else under it */
            int aw = text_width(1, artist);
            if (xe + 10 + aw <= LX + LW)
                text_at(1, xe + 10, ty + 15, artist, aw + 1, c_art);
            else {
                text_at(1, LX, y, artist, LW, c_art);
                y += 20;
            }
        }
        fill_rect(160, 28, 160, 143, RULE);
        upnext_draw(168, 33, 144, paused, (u32)UPN_N);   /* rows 33, 46, ... 137 */
        y = format_left(LX, y, LW, 140, paused);
        lyric_left(LX, y + 2, LW, 140, lyrics_line(), paused ? GREY : WHITE);
    }
    progress_at(Y_PROGRESS, paused);
    top_bar_3(qpos, out, right_colour, pct, pct_colour());
    (void)playing;
    return;
#endif
}

/* ---- player strip under RECHO's picture (Echo) ---------------------------------------
 * Two 320 x 26 rows, each drawn by the caller into a borrowed frame-buffer corner.
 *   row 0: the lyric line being sung when the track has a .lrc (lyrics.c), otherwise
 *          Now Playing: the format line; other screens: "▶ Artist - Song"
 *   row 1: progress bar and times */
#ifdef ECHO_PORT
extern const char *lyrics_line(void);

void footer_row(int row, int on_now_playing)
{
    int loaded = queue_loaded();
    int paused = player_state() == 2;
    if (!loaded)
        return;                           /* nothing queued: the strip stays empty */
    if (on_now_playing) {                 /* the lyric is in the picture: row 0 empty, the rule on top */
        if (row == 1)                     /* of row 1 (echo_band.c), the progress bar under it */
            progress_at(13, paused);
        return;
    }
    if (row == 0) {
        char info[64];                    /* file details, centred: format, rate, bits, kbps, "3/500" */
        format_line_ex(info, sizeof info, 1);
        text_centred(text_width(1, info) <= 300 ? 1 : 0, W / 2, 13, info, 300, paused ? DIM : GREY);
    } else {
        progress_at(13, paused);
    }
}
#endif

/* ---- cable chooser ------------------------------------------------------------- */

static void outline(int x0, int y0, int x1, int y1, u32 c)
{
    fill_rect(x0, y0, x1, y0, c);
    fill_rect(x0, y1, x1, y1, c);
    fill_rect(x0, y0, x0, y1, c);
    fill_rect(x1, y0, x1, y1, c);
}

static void battery_glyph(int cx, int y, u32 c, u32 lit_cells)
{
    int x = cx - 28;
    outline(x, y, x + 52, y + 24, c);
    outline(x + 1, y + 1, x + 51, y + 23, c);
    fill_rect(x + 53, y + 8, x + 56, y + 16, c);
    for (u32 i = 0; i < 4; i++)
        fill_rect(x + 5 + (int)i * 12, y + 5, x + 14 + (int)i * 12, y + 19, i < lit_cells ? c : RULE);
}

/* RECHO's own USB icon (U+E013, 16 x 9) drawn 3x: 48 x 27 */
static const u16 USB_ICON[9] = {0x0C00, 0x0FC0, 0x2020, 0x6013, 0xFFFF, 0x6083, 0x2100, 0x0E00, 0x0C00};

static void drive_glyph(int cx, int y, u32 c)
{
    int x0 = cx - 24;
    for (int r = 0; r < 9; r++) {
        u32 bits = USB_ICON[r];
        for (int col = 0; col < 16; col++) {
            if (!(bits >> col & 1u))
                continue;
            int run = col;
            while (run + 1 < 16 && (bits >> (run + 1) & 1u))
                run++;
            fill_rect(x0 + col * 3, y + r * 3, x0 + run * 3 + 2, y + r * 3 + 2, c);
            col = run;
        }
    }
}

void usb_draw(void)
{
    fill_rect(0, 0, W - 1, 169, BLACK);
    top_bar("Cable connected");
    u32 sel = USB_CURSOR ? 1u : 0u;
    fill_rect(W / 2, 32, W / 2, 160, RULE);
    outline(sel ? W / 2 + 6 : 6, 28, sel ? W - 7 : W / 2 - 6, 164, WHITE);

    int pm = BATTERY[1];
    u32 cells = pm <= 0 ? 0u : (u32)(pm + 249) / 250u;
    if (cells > 4)
        cells = 4;
    u32 c0 = sel ? GREY : WHITE, c1 = sel ? WHITE : GREY;
    battery_glyph(W / 4, 52, c0, cells);
    text_centred(1, W / 4, 104, "Charge only", 140, c0);
    char sub[32] = "power off and charge";
    if (USB_COUNTING) {
        u32 left = (USB_START + 5000u - ms_now()) / 1000u + 1u;
        char *p = put_str(sub, "in ", sub + 31);
        p = put_num(p, left > 5u ? 5u : left, sub + 31);
        p = put_str(p, " s", sub + 31);
        *p = 0;
    }
    text_centred(0, W / 4, 126, sub, 140, sel ? DIM : SOFT);

    drive_glyph(3 * W / 4, 50, c1);
    text_centred(1, 3 * W / 4, 104, "USB disk", 140, c1);
    text_centred(0, 3 * W / 4, 126, "files on the computer", 140, sel ? SOFT : DIM);
}

#ifndef ECHO_PORT
static char *put_pct(char *p, char *end)
{
    int pm = BATTERY[1];
    u32 pct = pm <= 0 ? 0u : (pm >= 1000 ? 100u : (u32)pm / 10u);
    p = put_num(p, pct, end);
    return put_str(p, "%", end);
}

/* battery % colour, as the Echo's Up Next header: green on the cable, red <= 15 %, amber <= 35 % */
static u32 pct_colour(void)
{
    int pm = BATTERY[1];
    if (BATTERY[3] != 0)
        return GREEN;
    return pm <= 150 ? RED : (pm <= 350 ? AMBER : WHITE);
}
#endif

/* ---- HINTS layer -------------------------------------------------------------------- */

void hints_stub(void)
{
#ifdef ECHO_PORT
    hints_dirty = 1;
#else
    const char *m = bar_message();
    if (m && *m) {
        hints_draw();                     /* RECHO's own bar: with a message it draws only that */
        msg_shown = 1;
    } else if (msg_shown) {
        msg_shown = 0;                    /* message gone: let the screen repaint those rows */
        const volatile u32 *scr = current_screen();
        if (scr)
            request_redraw((const void *)scr);
    }
#endif
}


/* ---- main menu (both players): Draft A, pixel art ------------------------------------
 * Replaces module m47's draw (descriptor 0x307AA88 + 36). RECHO's own menu logic stays:
 * its row order (patched: Files, USB disk, Now playing, Bluetooth, Settings), cursor
 * (state 0x0303BCC0 + 4), keys and select cases. RECHO's About and Supporters panels
 * (state + 24 / + 32) are still drawn by RECHO's own draw. */
#define MENU_STATE      PTR(volatile u32 *, 0x0303BCC0)
#define menu_draw_orig  FN(0x0307A2B8, void (*)(void))
#define FB              PTR(volatile u16 *, 0x03008A00)
#define TAB_DIM         0x4A69u

/* 12 x 12 pixel-art icons, drawn 2x (24 px). Square pixels only. */
static const char *const ICON_FILES[12] = {"............", ".####.......", ".#..#.......", ".#..#######.", ".#........#.", ".##########.", ".#........#.", ".#........#.", ".#........#.", ".#........#.", ".##########.", "............"};
static const char *const ICON_USB[12] = {"............", "............", "........##..", ".....####...", "....#.....#.", "##.#......##", "############", "##....#...##", ".......#..#.", "........##..", "........##..", "............"};
static const char *const ICON_PLAY[12] = {"............", "....#.......", "....##......", "....###.....", "....####....", "....#####...", "....#####...", "....####....", "....###.....", "....##......", "....#.......", "............"};
static const char *const ICON_BT[12] = {"............", ".....#......", ".....##.....", "..#..#.#....", "...#.#.#....", "....###.....", "....###.....", "...#.#.#....", "..#..#.#....", ".....##.....", ".....#......", "............"};
static const char *const ICON_SETTINGS[12] = {"............", ".....##.....", "..#.####.#..", "..########..", "...##..##...", ".###....###.", ".###....###.", "...##..##...", "..########..", "..#.####.#..", ".....##.....", "............"};


/* the large font at 2x: drawn into the tab rows first (cleared again after), then doubled */
static void text2x(int x, int y_top, const char *s, u32 c)
{
    const int sy = 120;
    int h = font_h(1), w = text_width(1, s);
    if (w > 150)
        w = 150;
    fill_rect(0, sy, w + 1, sy + h, BLACK);
    draw_text(1, 0, sy, s, w, c, -1);
    for (int r = h - 1; r >= 0; r--)
        for (int col = 0; col < w; col++) {
            u16 px = FB[(sy + r) * W + col];
            int dx = x + col * 2, dy = y_top + r * 2;
            if (dx + 1 >= W)
                break;
            FB[dy * W + dx] = px;
            FB[dy * W + dx + 1] = px;
            FB[(dy + 1) * W + dx] = px;
            FB[(dy + 1) * W + dx + 1] = px;
        }
}

/* right side of the top bars: the volume as RECHO's dB + 90, then the output in use
 * ("62 dB . 3.5 mm", "muted . 4.4 mm"); "BT <headphones>" in green while Bluetooth is connected */
static const char *output_label(char *buf, int n, u32 *colour)
{
    char *p = buf, *e = buf + n - 1;
    *colour = SOFT;
    int db = VOLUME_DB;
    if (db <= -128) {
        p = put_str(p, "muted", e);
    } else {
        p = put_num(p, (u32)(db + 90 > 0 ? db + 90 : 0), e);
#ifdef ECHO_PORT
        p = put_str(p, " dB", e);
#else
        p = put_str(p, "dB", e);
#endif
    }
#ifdef ECHO_PORT
#define OSEP SEP
#else
#define OSEP "\xee\x80\xa1"                    /* Mini: "62 dB·3.5mm", the dot close on both sides */
#endif
    if (bt_state == BT_CONNECTED && bt_conn_name[0]) {
        p = put_str(p, OSEP, e);
#ifdef ECHO_PORT
        p = put_str(p, "BT ", e);
#endif
        p = put_str(p, bt_conn_name, e);
        *colour = GREEN;
    } else if (queue_loaded()) {
        const char *o = output_name();
        if (o && *o) {
            p = put_str(p, OSEP, e);
            p = put_str(p, o, e);
        }
    }
    *p = 0;
    return buf;
}

static const char *bt_words(u32 *colour)
{
    *colour = SOFT;
    switch (bt_state) {
    case 1: return "Starting";
    case 2: *colour = RED; return "Not answering";
    case 3: return "On";
    case 4: *colour = AMBER; return "Searching";
    case 5: *colour = AMBER; return "Connecting";
    case 6: *colour = GREEN; return 0;
    default: return "Off";
    }
}

struct menu_item { const char *title; const char *const *icon; const char *tab; };
static const struct menu_item MENU_ITEMS[5] = {
    {"Files", ICON_FILES, "Files"},       {"USB disk", ICON_USB, "USB disk"},
    {"Now playing", ICON_PLAY, "Playing"}, {"Bluetooth", ICON_BT, "Bluetooth"},
    {"Settings", ICON_SETTINGS, "Settings"}};

/* tab glyphs: RECHO's own icons (folder U+E012, disc U+E011, wrench U+E015), "sys" for the
 * USB disk, and the pixel Bluetooth rune; the selected one between "<" and ">" */
/* the owner's 16 px tab icons (folder 16 x 14, disc 16 x 16, wrench 16 x 15), square pixels */
static const char *const TAB_FOLDER[14] = {
    "..#####.........",
    ".#.....#........",
    "#.......#.......",
    "#........######.",
    "#..............#",
    "#..............#",
    "#..............#",
    "#..............#",
    "#..............#",
    "#..............#",
    "#..............#",
    "#..............#",
    ".#............#.",
    "..############.."};
static const char *const TAB_DISC[16] = {
    ".....######.....",
    "...##########...",
    "..###.########..",
    ".###.#.########.",
    ".##.#.#########.",
    "####.#....######",
    "#####......#####",
    "#####..##..#####",
    "#####..##..#####",
    "#####......#####",
    "######....#.####",
    ".#########.#.##.",
    ".########.#.###.",
    "..########.###..",
    "...##########...",
    ".....######....."};
static const char *const TAB_WRENCH[15] = {
    ".....####.......",
    "....#....#......",
    ".....#....#.....",
    "......#....#....",
    ".#.....#...#....",
    "#.#....#...#....",
    "#..#...#...#....",
    "#...###....#....",
    "#..........#....",
    ".#..........#...",
    "..#..........#..",
    "...######.....#.",
    ".........#.....#",
    "..........#.....",
    "...........#...."};

static const char *const TAB_SYS[12] = {
    ".####..##...##..####.",
    "######.##...##.######",
    "##..##..##..#..##..##",
    "###.....##.##..###...",
    ".####...##.##...####.",
    "...###..##.##.....###",
    "##..##...###...##..##",
    "######...###...######",
    ".####....###....####.",
    ".........##..........",
    "........###..........",
    "........##..........."};
static const char *const TAB_BT[16] = {
    "......##....",
    "......####..",
    "......####..",
    "##....##..##",
    "##....##..##",
    "..##..##..##",
    "..##..##..##",
    "....######..",
    "....######..",
    "..##..##..##",
    "..##..##..##",
    "##....##..##",
    "##....##..##",
    "......####..",
    "......####..",
    "......##...."};

static void bitmap1x(const char *const *rows, int n, int x0, int y0, u32 c)
{
    for (int r = 0; r < n; r++)
        for (int col = 0; rows[r][col]; col++) {
            if (rows[r][col] != '#')
                continue;
            int run = col;
            while (rows[r][run + 1] == '#')
                run++;
            fill_rect(x0 + col, y0 + r, x0 + run, y0 + r, c);
            col = run;
        }
}

static void menu_tab(u32 i, int cx, int cy, u32 c)
{
    switch (i) {
    case 0:
        bitmap1x(TAB_FOLDER, 14, cx - 8, cy - 7, c);
        break;
    case 1:
        bitmap1x(TAB_SYS, 12, cx - 10, cy - 4, c);
        break;
    case 2:
        bitmap1x(TAB_DISC, 16, cx - 8, cy - 8, c);
        break;
    case 3:
        bitmap1x(TAB_BT, 16, cx - 6, cy - 8, c);
        break;
    default:
        bitmap1x(TAB_WRENCH, 15, cx - 8, cy - 7, c);
        break;
    }
}

void menu_draw(void)
{
    if (MENU_STATE[6] || MENU_STATE[8]) {
        menu_draw_orig();
        return;
    }
    MENU_STATE[0] = 5u;                     /* row count: RECHO's draw sets it, its keys need it */
    u32 cur = MENU_STATE[1];
    if (cur > 4u)
        cur = 4u;
    const struct menu_item *it = &MENU_ITEMS[cur];
    fill_rect(0, 0, W - 1, 169, BLACK);

    text2x(16, 28, it->title, WHITE);
    fill_rect(0, 104, W - 1, 169, BLACK);

    char right[48];
    u32 rc;
    const char *r = output_label(right, sizeof right, &rc);
#ifdef ECHO_PORT
    top_bar_c(r, rc);
#else
    {                                       /* Mini: play mode + output, then the battery in white */
        static const char MODE[5][4] = {"\xee\x80\x83", "\xee\x80\x84", "\xee\x80\x85",
                                        "\xee\x80\x86", "\xee\x80\xab"};
        char pct[8], lead[64];
        char *q = put_pct(pct, pct + 7);
        *q = 0;
        q = lead;
        u32 m = PLAY_MODE;
        if (m < 5u) {
            q = put_str(q, MODE[m], lead + 63);
            if (r)
                q = put_str(q, " ", lead + 63);
        }
        q = put_str(q, r, lead + 63);
        *q = 0;
        top_bar_2(lead, rc, pct, pct_colour());
    }
#endif

    int paused = player_state() == 2;
    const char *sub = 0, *hint = 0;
    u32 sc = SOFT;
    char tmp[48];
    switch (cur) {
    case 0:
        sub = "Your music folders";
        break;
    case 1:
        sub = "Show the player as a drive";
        break;
    case 2:
        if (queue_loaded()) {
            const char *artist;
            const char *title = track_title(&artist);
            char *p = tmp;
            if (artist) {
                p = put_str(p, artist, tmp + 47);
                p = put_str(p, " - ", tmp + 47);
            }
            p = put_str(p, title, tmp + 47);
            *p = 0;
            sub = tmp;
            sc = paused ? GREY : SOFT;
            hint = paused ? "Paused" : "Playing";
        } else {
            sub = "Nothing playing";
            hint = "Pick a song in Files";
        }
        break;
    case 3:
        sub = bt_words(&sc);
        if (!sub) {
            char *p = put_str(tmp, "Connected to ", tmp + 47);
            p = put_str(p, bt_conn_name, tmp + 47);
            *p = 0;
            sub = tmp;
        }
        break;
    default:
        sub = "Play mode, screen, clock";
        break;
    }
#ifdef ECHO_PORT
    /* Echo: both lines in the large font, then one row of tabs (no rule, as on the Mini) */
    if (sub)
        text_at(1, 16, 72, sub, W - 32, sc);
    if (hint)
        text_at(1, 16, 98, hint, W - 32, GREY);
    const int tab_y = 150;
#else
    /* Mini (drawing): both lines small, one row of tabs, the progress bar under them */
    if (sub)
        text_at(0, 16, 70, sub, W - 32, sc);
    if (hint)
        text_at(0, 16, 87, hint, W - 32, GREY);
    const int tab_y = 125;
#endif
    for (u32 i = 0; i < 5u; i++) {
        int cx = (int)i * 64 + 32;
        u32 on = i == cur;
        menu_tab(i, cx, tab_y, on ? WHITE : GREY);
        if (on) {
            text_at(1, cx - 24, tab_y, "<", 10, WHITE);
            text_at(1, cx + 14, tab_y, ">", 10, WHITE);
        }
    }
#ifndef ECHO_PORT
    if (queue_loaded())
        progress_at(158, paused);
#endif
}

/* Settings -> About: RECHO's own About panel in the menu module, back to Settings after */
STATE volatile u32 about_state = 0;
#define CURRENT_MODULE_ID REG(0x030399C8)
#define set_module_id     FN(0x0305B234, void (*)(int module))
const char str_about[] = "About";

void about_request_open(void)
{
    about_state = 1;
}

STATE volatile u32 menu_last_module = 0;
STATE volatile int vol_shown = -1000;

/* RECHO's display safety (0x306C018): a boot that ends within 5 s of trying the fast VOP bus
 * leaves "VOP1" at 0x0309FFB0; the next boot then writes "VOPF" and uses the slow GPIO path, and
 * keeps doing so while RAM stays powered (on a cable, "off" is the charging screen), because only
 * 5 s on VOP clears it. Up Next and the Echo strip need VOP. After 5 s on the slow path we clear
 * "VOPF", so the next start tries VOP again; a real VOP failure is still caught by RECHO then. */
#define VOP_MARK  REG(0x0309FFB0)
#define log_printf FN(0x0306C9EC, void (*)(const char *fmt, ...))
#define VOP_FAILED 0x564F5046u                     /* "VOPF" */

void about_tick(void)
{
    if (VOP_MARK == VOP_FAILED && ms_now() > 5000u) {
        VOP_MARK = 0;                       /* next start tries the fast display path again */
        if (((volatile unsigned char *)lit(0x0303EDD0u))[5])
            log_printf("display: VOPF fallback cleared after %u ms", ms_now());
    }
    u32 m = CURRENT_MODULE_ID;
    if (VOLUME_DB != vol_shown) {             /* top bars show the volume: follow it */
        vol_shown = VOLUME_DB;
        if (m == 45u || m == 47u) {
            const volatile u32 *sv = current_screen();
            if (sv)
                request_redraw((const void *)sv);
        }
    }
    if (m == 47u && menu_last_module != 47u && about_state == 0u) {
        MENU_STATE[1] = 2u;                 /* the menu opens on Now playing */
        const volatile u32 *scr0 = current_screen();
        if (scr0)
            request_redraw((const void *)scr0);
    }
    menu_last_module = m;
    if (about_state == 1) {
        if (CURRENT_MODULE_ID != 47u) {
            set_module_id(47);
            return;
        }
        MENU_STATE[6] = 1;
        const volatile u32 *scr = current_screen();
        if (scr)
            request_redraw((const void *)scr);
        about_state = 2;
    } else if (about_state == 2) {
        if (CURRENT_MODULE_ID != 47u)
            about_state = 0;
        else if (!MENU_STATE[6]) {
            about_state = 0;
            set_module_id(48);
        }
    }
}

/* RECHO's "%d dB" volume text (menu bar 0x30594D4, volume message 0x305A4B0) goes through here:
 * shown from 0 dB at RECHO's quietest step (-90 dB) upward, so -43 dB reads "47 dB". */
int vol_text(char *buf, u32 size, const char *fmt, int v)
{
    (void)fmt;
    char *p = buf, *end = buf + (size ? size - 1u : 0u);
    v += 90;
    if (v < 0) {
        p = put_str(p, "-", end);
        v = -v;
    }
    p = put_num(p, (u32)v, end);
    p = put_str(p, " dB", end);
    *p = 0;
    return (int)(p - buf);
}

/* Boot screen (replaces RECHO's "RECHO 0.10.1" splash, `bl 0x306C3E4` at 0x3050D7C): a black frame.
 * RECHO draws it at ~0.17 s, before it loads its fonts (~0.20 s), and goes straight on starting up
 * (it does not wait after the splash), so the frame shows for a split second only. *rect gets the
 * whole screen so RECHO pushes all of it to the panel. */
int boot_screen(const char *s, int a, int b, u32 colour, int z, int *rect)
{
    (void)s; (void)a; (void)b; (void)colour; (void)z;
    fill_rect(0, 0, W - 1, 169, BLACK);
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = W - 1;
    rect[3] = 169;
    return 1;
}
