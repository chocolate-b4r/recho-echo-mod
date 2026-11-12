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
#define rg_mode      FN(0x03057DB4, int (*)(void))
#define rg_gain      FN(0x03057DC0, int (*)(void))

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
        return "4.4 mm";
    if (j & 1u)
        return "3.5 mm";
    return 0;
}

#ifndef ECHO_PORT
static char *put_pct(char *p, char *end);
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
#endif

/* ---- helpers ------------------------------------------------------------------ */

static int font_h(int font)
{
    int h = FONTS[font * 4 + 2];
    return (h > 0 && h < 40) ? h : 12;
}

/* text with its vertical centre on y_mid; returns the width drawn */
static int text_at(int font, int x, int y_mid, const char *s, int maxw, u32 fg)
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


/* ---- top bar shared by Now Playing and the chooser --------------------------- */

static void top_bar_c(const char *right, u32 right_colour);

static void top_bar(const char *right)
{
    top_bar_c(right, SOFT);
}

static void top_bar_2(const char *right, u32 right_colour, const char *last, u32 last_colour)
{
    char t[8], d[16];
    clock_strings(t, d);
    int x = 8 + text_at(1, 8, 10, t, 60, WHITE);
    fill_rect(x + 5, 4, x + 5, 16, GREY);                 /* "21:47 | Fri 13 Nov" */
    text_at(1, x + 11, 10, d, 120, SOFT);
    int xr = W - 8;
    if (last && *last) {
        int w = text_width(1, last);
        xr -= w;
        text_at(1, xr, 10, last, 60, last_colour);
        xr -= 8;
    }
    if (right && *right) {
        int w = text_width(1, right);
        if (w > 140)
            w = 140;
        text_at(1, xr - w, 10, right, 140, right_colour);
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

static void format_line_ex(char *out, int n, u32 *rg_colour_from, int full);

#ifndef ECHO_PORT
static void format_line(char *out, int n, u32 *rg_colour_from)
{
    format_line_ex(out, n, rg_colour_from, 1);
}
#endif

/* file type, rate, bits (or kbps); with full also the output and ReplayGain */
static void format_line_ex(char *out, int n, u32 *rg_colour_from, int full)
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
        p = put_str(p, "  ", end);
    }
    const volatile u32 *f = fmt_info();
    if (f && f[0]) {
        u32 r = f[0];
        p = put_num(p, r / 1000u, end);
        if (r % 1000u >= 100u) {
            *p++ = '.';
            p = put_num(p, (r % 1000u) / 100u, end);
        }
        p = put_str(p, " kHz", end);
        if (f[3]) {
            p = put_str(p, SEP, end);
            p = put_num(p, f[3], end);
            p = put_str(p, " kbps", end);
        } else if (f[2]) {
            p = put_str(p, SEP, end);
            p = put_num(p, f[2], end);
            p = put_str(p, " bit", end);
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
    *rg_colour_from = 0;
    if (!full) {
        *p = 0;
        return;
    }
    {                                     /* Mini: queue position here; the output is in the top bar */
        u32 qn;
        play_position(&qn);
        if (qn) {
            if (p != out)
                p = put_str(p, SEP, end);
            p = put_qpos(p, end);
        }
    }
    *rg_colour_from = 0;
    int mode = rg_mode();
    if (mode >= 1 && mode <= 3) {
        if (p != out)
            p = put_str(p, SEP, end);
        *rg_colour_from = (u32)(p - out);
        int g = rg_gain();
        u32 a = (u32)(g < 0 ? -g : g);
        u32 tenths = (a + 5u) / 10u;
        p = put_str(p, "RG ", end);
        if (tenths)
            p = put_str(p, g < 0 ? "-" : "+", end);
        p = put_num(p, tenths / 10u, end);
        *p++ = '.';
        p = put_num(p, tenths % 10u, end);
        p = put_str(p, " dB", end);
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

static const char *track_album(void)
{
    const char *tags = tag_record(queue_index());
    if (tags && tags[0])
        return tags[256] ? tags + 256 : 0;
    return without_ext(folder_name(), album_buf, sizeof album_buf);
}

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

#ifdef ECHO_PORT
static void track_line_at(int x, int y, int paused)
{
    track_line_xy(x, y, paused);
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
static void format_at(int y, int paused)
{
    char line[96];
    u32 rg_at = 0;
    format_line(line, sizeof line, &rg_at);
    int wl = text_width(0, line);
    if (wl > 300)
        wl = 300;
    int x = (W - wl) / 2;
    if (rg_at && !paused) {
        char keep = line[rg_at];
        line[rg_at] = 0;
        int w1 = text_at(0, x, y, line, 300, GREY);
        line[rg_at] = keep;
        text_at(0, x + w1, y, line + rg_at, 300 - w1, AMBER);
    } else {
        text_at(0, x, y, line, 300, paused ? DIM : GREY);
    }
}
#endif

static const char *output_label(char *buf, int n, u32 *colour);
#ifdef ECHO_PORT
extern const char *lyrics_line(void);
#define FB_PX PTR(volatile u16 *, 0x03008A00)
/* draw "a" then "b" (either may be 0) in the large font at 2x, centred on cx, top at y_top.
 * Rendered first into scratch rows 152..(152 + h) of the frame buffer, cleared afterwards.
 * Returns 0 (drawing nothing) when it would not fit in 300 px. */
static int text2x_line(int cx, int y_top, const char *a, u32 ca, const char *b, u32 cb)
{
    const int sy = 152;                   /* below the 2x lyric (rows 111..140); nothing else drawn there */
    int h = font_h(1);
    int wa = (a && *a) ? text_width(1, a) : 0, ws = wa ? text_width(1, " - ") : 0;
    int wb = (b && *b) ? text_width(1, b) : 0, w = wa + ws + wb;
    if (!w || w > 150)
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
    int x0 = cx - w;
    for (int r = 0; r < h; r++)
        for (int col = 0; col < w; col++) {
            u16 px = FB_PX[(sy + r) * W + col];
            int dx = x0 + col * 2, dy = y_top + r * 2;
            FB_PX[dy * W + dx] = px;
            FB_PX[dy * W + dx + 1] = px;
            FB_PX[(dy + 1) * W + dx] = px;
            FB_PX[(dy + 1) * W + dx + 1] = px;
        }
    fill_rect(0, sy, w + 1, sy + h, BLACK);
    return 1;
}
#endif

#ifndef ECHO_PORT
/* "Next  Artist - Title" in the small font: the next entry in the play order (RECHO's own
 * next routine), its tags from RECHO's tag record (read in the background; "..." until then) */
static void next_line(int y, int paused)
{
    if (!QUEUE_ACTIVE)
        return;
    int nx = queue_next_entry(QUEUE_OBJ, QUEUE_CURRENT, PLAY_MODE, 0);
    const char *qinfo_end = 0;
    const volatile int *qi = queue_info();
    u32 n = (qi && qi[1] > 0) ? (u32)qi[1] : 0u;
    char line[96], *p = line, *end = line + 95;
    if (nx < 0 || (u32)nx >= n || (u32)nx == QUEUE_CURRENT) {
        qinfo_end = "End of queue";
    } else {
        const char *t = tag_record(nx);
        if (t && t[0]) {
            if (t[160]) {
                p = put_str(p, t + 160, end);
                p = put_str(p, " - ", end);
            }
            p = put_str(p, t, end);
        } else {
            p = put_str(p, "...", end);
        }
    }
    *p = 0;
    int wl = text_width(0, "Next  ");
    const char *body = qinfo_end ? qinfo_end : line;
    int wb = text_width(0, body);
    int total = wl + (wb > 280 - wl ? 280 - wl : wb), x = (W - total) / 2;
    text_at(0, x, y, "Next  ", wl + 1, DIM);
    text_at(0, x + wl, y, body, 280 - wl, paused ? DIM : GREY);
}
#endif

void np_draw(void)
{
    fill_rect(0, 0, W - 1, 169, BLACK);
    int loaded = queue_loaded();
    char qpos[24];
    qpos[0] = 0;
    char *p = qpos;
    if (loaded) {
        p = put_qpos(qpos, qpos + 23);
    }
#ifdef ECHO_PORT
    /* draft C: output in the top bar; "BT <headphones>" in green while Bluetooth is connected */
    u32 right_colour = SOFT;
    if (bt_state == BT_CONNECTED && bt_conn_name[0]) {
        p = put_str(qpos, "BT ", qpos + 23);
        p = put_str(p, bt_conn_name, qpos + 23);
        right_colour = GREEN;
    } else {
        p = put_str(qpos, loaded ? output_name() : 0, qpos + 23);
    }
#else
    /* Mini: play-mode glyph and the output ("3.5 mm", "BT <headphones>" in green), then the
     * battery in white; the queue position moved to the format line */
    u32 right_colour = SOFT;
    {
        static const char MODE[5][4] = {"\xee\x80\x83", "\xee\x80\x84", "\xee\x80\x85",
                                        "\xee\x80\x86", "\xee\x80\xab"};
        char out[40];
        const char *o = output_label(out, sizeof out, &right_colour);
        u32 m = PLAY_MODE;
        p = qpos;
        if (loaded && m < 5u) {
            p = put_str(p, MODE[m], qpos + 23);
            if (o)
                p = put_str(p, " ", qpos + 23);
        }
        p = put_str(p, o, qpos + 23);
    }
#endif
    *p = 0;
#ifdef ECHO_PORT
    top_bar_c(qpos, right_colour);
#else
    {
        char pct[8];
        char *q = put_pct(pct, pct + 7);
        *q = 0;
        top_bar_2(qpos, right_colour, pct, WHITE);
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
        if (!text2x_line(W / 2, 24, artist, c_art, title, c_title))
            track_line(40, paused);
        text_centred(1, W / 2, 68, track_album(), 300, paused ? DIM : GREY);
        char info[64];
        u32 rg_unused;
        format_line_ex(info, sizeof info, &rg_unused, 0);
        {                                 /* queue position after the format, e.g. "3/500" */
            char *q = info, *qe = info + sizeof info - 1;
            while (*q)
                q++;
            u32 qn;
            play_position(&qn);
            if (qn) {
                if (q != info)
                    q = put_str(q, SEP, qe);
                q = put_qpos(q, qe);
            }
            *q = 0;
        }
        text_centred(0, W / 2, 88, info, 300, paused ? DIM : GREY);
        fill_rect(60, 100, W - 61, 100, RULE);

        u32 lc = paused ? GREY : WHITE;
        if (ms_now() - VOLUME_AT < 2000u) {
            char v[24], *q = put_str(v, "Volume ", v + 23);
            int db = VOLUME_DB;
            if (db <= -128)
                q = put_str(q, "muted", v + 23);
            else {
                q = put_num(q, (u32)(db + 90 > 0 ? db + 90 : 0), v + 23);
                q = put_str(q, " dB", v + 23);
            }
            *q = 0;
            text_centred(1, W / 2, 126, v, 300, WHITE);
        } else {
            const char *ly = lyrics_line();
            if (ly && !text2x_line(W / 2, 111, 0, 0, ly, lc)) {
                int f = text_width(1, ly) <= 304 ? 1 : 0;
                text_centred(f, W / 2, 126, ly, 304, lc);
            }
        }
    }
    (void)playing;
    return;
#else
    /* Artist - Song, album */
    track_line(Y_TITLE, paused);
    const char *album = track_album();
    text_centred(1, W / 2, Y_ALBUM, album, 300, paused ? DIM : GREY);
#endif

#ifndef ECHO_PORT
    /* Mini: the next song in one small line, or the volume for 2 s after it changed */
    if (ms_now() - VOLUME_AT < 2000u) {
        char v[24], *q = put_str(v, "Volume ", v + 23);
        int db = VOLUME_DB;
        if (db <= -128)
            q = put_str(q, "muted", v + 23);
        else {
            q = put_num(q, (u32)(db + 90 > 0 ? db + 90 : 0), v + 23);
            q = put_str(q, " dB", v + 23);
        }
        *q = 0;
        text_centred(1, W / 2, Y_NEXT, v, 300, WHITE);
    } else {
        next_line(Y_NEXT, paused);
    }
    progress_at(Y_PROGRESS, paused);
    format_at(Y_FORMAT, paused);
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
extern const char *lyrics_next_line(void);

void footer_row(int row, int on_now_playing)
{
    int loaded = queue_loaded();
    int paused = player_state() == 2;
    if (!loaded) {
        if (row == 0)
            text_centred(1, W / 2, 13, "Nothing playing", 300, GREY);
        return;
    }
    if (row == 0 && on_now_playing) {
        const char *nl = lyrics_next_line();
        if (nl) {
            int font = text_width(1, nl) <= 304 ? 1 : 0;
            text_centred(font, W / 2, 13, nl, 304, DIM);
        }
        return;
    }
    if (row == 0) {
        const char *ly = lyrics_line();
        if (ly) {
            int font = text_width(1, ly) <= 304 ? 1 : 0;   /* long lines: the small font */
            text_centred(font, W / 2, 13, ly, 304, paused ? GREY : WHITE);
            return;
        }
    }
    if (on_now_playing) {                 /* format line is in the picture (draft C) */
        if (row == 1)
            progress_at(13, paused);
        return;
    }
    if (row == 0) {
        int x0 = 10;
        if (paused) {
            fill_rect(x0, 6, x0 + 3, 19, RED);
            fill_rect(x0 + 7, 6, x0 + 10, 19, RED);
        } else {
            for (int i = 0; i < 14; i++) {
                int w = i < 7 ? i * 2 + 1 : (13 - i) * 2 + 1;
                fill_rect(x0, 6 + i, x0 + w * 6 / 14, 6 + i, WHITE);
            }
        }
        track_line_at(x0 + 20, 13, paused);
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

/* right side of the top bars: "BT <headphones>" in green, or the output in use */
static const char *output_label(char *buf, int n, u32 *colour)
{
    *colour = SOFT;
    if (bt_state == BT_CONNECTED && bt_conn_name[0]) {
        char *p = put_str(buf, "BT ", buf + n - 1);
        p = put_str(p, bt_conn_name, buf + n - 1);
        *p = 0;
        *colour = GREEN;
        return buf;
    }
    return queue_loaded() ? output_name() : 0;
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
    case 1: {
        int w = text_width(1, "sys");
        text_at(1, cx - w / 2, cy, "sys", 40, c);
        break;
    }
    case 2:
        bitmap1x(TAB_DISC, 16, cx - 8, cy - 8, c);
        break;
    case 3:
        bitmap1x(ICON_BT, 12, cx - 6, cy - 6, c);
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

    char right[40];
    u32 rc;
    const char *r = output_label(right, sizeof right, &rc);
#ifdef ECHO_PORT
    top_bar_c(r, rc);
#else
    {                                       /* Mini: output in its colour, then the battery in white */
        char pct[8];
        char *q = put_pct(pct, pct + 7);
        *q = 0;
        top_bar_2(r, rc, pct, WHITE);
    }
#endif

    int paused = player_state() == 2;
    const char *sub = 0, *hint = 0;
    u32 sc = SOFT;
    char tmp[48];
    switch (cur) {
    case 0:
        sub = "Your music folders";
        hint = "Hold Play on a folder: play it all";
        break;
    case 1:
        sub = "Show the player as a drive";
        hint = "Plug in the cable first";
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
        hint = "Search and connect headphones";
        break;
    default:
        sub = "Play mode, screen, clock";
        hint = "About is at the bottom";
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
            text_at(1, cx - 22, tab_y, "<", 10, WHITE);
            text_at(1, cx + 16, tab_y, ">", 10, WHITE);
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

void about_tick(void)
{
    u32 m = CURRENT_MODULE_ID;
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
