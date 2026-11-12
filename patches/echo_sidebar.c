/*
 * RECHO on the full-size Echo — stage 2: "Up Next" sidebar in the right
 * 160 x 222 column (panel columns 320..479, rows 49..270).
 *
 * Data (all RECHO's own):
 *   queue object   0x0303AB90   [4] count, [8] seed, [24] shuffle pass
 *   queue active   0x0303BD74   current entry 0x0303BD78
 *   play mode      byte 7 of the settings block 0x0303EDD0
 *   next entry     0x3065FE4(q, cur, mode, 0)  pure: the queue has no skip predicate
 *   entry name     0x3064B64(store 0x0303BD80, entry, buf, &vol, 0)
 *                  the player's own lookup: one 36-byte index read plus the M3U line,
 *                  copies the 276-byte file name into buf, returns 0 on success.
 *                  Before USB-disk mode RECHO clears the active flag and closes the
 *                  store (0x305BC30), so nothing here touches storage while the PC owns it.
 *
 * Drawing: RECHO only draws its 320 x 170 frame buffer. For each sidebar line we
 * borrow the buffer's top-left 160 x LINE_H corner: save it, draw the line there
 * with RECHO's own text routine (font 1, the status-bar font), push those pixels to
 * the sidebar's panel window, and put the saved pixels back. Runs in the main loop
 * after 0x306B534 has waited until no DMA band is in flight.
 *
 * Cost: one line per main-loop pass (at most one lookup + 160 x 18 pixels), so a
 * track change spreads over ~13 passes and playback is never held up for long.
 * RAM: strip 0x03044000 (16,640 B, shared with the info band), name 0x03048200 (276 B), inside the 29 KB no
 * RECHO code references (0x03043002..0x0304A240).
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

#define panel_send   FN(0x0306AF98, void (*)(u32 cmd, const unsigned char *data, u32 len))
#define panel_mode   FN(0x0306AF6C, void (*)(u32 mode))
#define push_idle    FN(0x0306B534, void (*)(void))
#define fill_rect    FN(0x0306BF2C, void (*)(int, int, int, int, u32))
#define text_width   FN(0x03058D20, int (*)(int font, const char *s))
#define draw_text    FN(0x03058D84, int (*)(int font, int x, int y, const char *s, int maxw, u32 fg, int bg))
#define ms_now       FN(0x03051638, u32 (*)(void))
#define queue_next   FN(0x03065FE4, int (*)(void *q, u32 cur, u32 mode, u32 flag))
#define queue_name   FN(0x03064B64, int (*)(void *store, u32 entry, char *buf, unsigned char *vol, u32 flag))

#define VOP_ACTIVE   REG(0x030089EC)
#define VOP_STATUS   REG(0x60070024)
#define VOP_DATA     REG(0x6007002C)
#define BL_DUTY      REG(0x03006174)                            /* backlight PWM value, 0 = off */
#define CLIP         PTR(volatile int *, 0x03004114)              /* x0, y0, x1, y1 */
#define FONTS        PTR(volatile int *, 0x03034B48)              /* 16 B per font: [2] height, [3] loaded */
#define FB           PTR(volatile u16 *, 0x03008A00)              /* 320 x 170 RGB565 */
#define STRIP        PTR(u16 *, 0x03044000)                     /* up to 160 x 52 px = 16,640 B */
#define NAMEBUF      PTR(char *, 0x03048200)                    /* 276 B */
#define UI_MAX_W     320
#define UI_MAX_PX    8320                       /* strip size in pixels (16,640 B) */
#define QUEUE        PTR(void *, 0x0303AB90)
#define QSTORE       PTR(void *, 0x0303BD80)
#define Q_ACTIVE     REG(0x0303BD74)
#define Q_CURRENT    REG(0x0303BD78)
#define Q_COUNT      REG(0x0303AB94)
#define Q_SEED       REG(0x0303AB98)
#define Q_PASS       REG(0x0303ABA8)
#define PLAY_MODE    (PTR(volatile unsigned char *, 0x0303EDD0)[7])       /* settings block, byte 7 */

#define SB_W      160
#define LINE_H    18
#define HEADER_H  22                      /* lines up with the screen's top bar */
#define LINES     11                      /* 22 + 11 x 18 = 220 of the 222 visible rows */
#define PANEL_X   320
#define PANEL_Y0  49                      /* top visible row, same as RECHO's picture */
#define NAME_MAX  276                     /* the lookup copies exactly this many bytes */
#define SETTLE_MS 300u                    /* let rapid skipping finish first */

#define BLACK 0x0000u
#define WHITE 0xF79Eu
#define GREY  0x8410u
#define SOFT  0xC618u
#define FONT  1

/* State lives in the cave (code RAM is plain SRAM; no MPU). */
#define STATE __attribute__((section(".text.sidebar_state,\"ax\",%progbits@"), used))
STATE volatile u32 sb_key[6] = {0};
STATE volatile u32 sb_hdr_key = 0xFFFFFFFFu;
STATE volatile u32 sb_dirty = 1;          /* panel was (re)initialised */
STATE volatile u32 sb_blank = 1;          /* areas outside RECHO's frame still need clearing */
STATE volatile u32 sb_line = 0;           /* next line to draw, 1-based; 0 = all drawn */
STATE volatile u32 sb_cursor = 0;         /* queue entry the walk has reached */
STATE volatile u32 sb_walk = 0;           /* 1 while upcoming entries remain */
STATE volatile u32 sb_changed_at = 0;

extern void echo_blank_outside(void);

/* "03 Song name.flac" -> "03 Song name" (only a short extension is dropped). */
void sidebar_clean_name(char *s)
{
    char *dot = 0, *p;
    for (p = s; *p; p++)
        if (*p == '.')
            dot = p;
    if (dot && dot != s && (p - dot) <= 5)
        *dot = 0;
}

/* ---- borrowing the frame buffer's top-left corner (shared with the band) ----
 * ui_borrow saves a w x h corner (w <= 320, w * h <= 8,320 px), clips RECHO's
 * drawing routines to it and clears it; ui_return pushes it to the panel at
 * (px, py) and puts RECHO's pixels and clip rectangle back. Callers first
 * make sure no DMA band is in flight (push_idle). */
STATE volatile int ui_clip[4] = {0};

void ui_borrow(int w, int h)
{
    if (w > UI_MAX_W) w = UI_MAX_W;
    if (w < 1) w = 1;
    if (h > UI_MAX_PX / w) h = UI_MAX_PX / w;
    for (int i = 0; i < 4; i++)
        ui_clip[i] = CLIP[i];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            STRIP[y * w + x] = FB[y * 320 + x];
    CLIP[0] = 0; CLIP[1] = 0; CLIP[2] = w - 1; CLIP[3] = h - 1;
    fill_rect(0, 0, w - 1, h - 1, BLACK);
}

void ui_return(int px, int py, int w, int h)
{
    if (w > UI_MAX_W) w = UI_MAX_W;
    if (w < 1) w = 1;
    if (h > UI_MAX_PX / w) h = UI_MAX_PX / w;
    if (VOP_ACTIVE) {
        const int x1 = px + w - 1, y1 = py + h - 1;
        unsigned char c[4] = {(unsigned char)(px >> 8), (unsigned char)px, (unsigned char)(x1 >> 8), (unsigned char)x1};
        unsigned char r[4] = {(unsigned char)(py >> 8), (unsigned char)py, (unsigned char)(y1 >> 8), (unsigned char)y1};
        panel_send(0x2A, c, 4);
        panel_send(0x2B, r, 4);
        panel_send(0x2C, 0, 0);
        panel_mode(2);
        for (int y = 0; y < h; y++) {
            volatile u16 *row = FB + y * 320;
            for (int x = 0; x < w; x++) {
                VOP_DATA = row[x];
                if ((x & 3) == 3)
                    for (u32 t = 2000; (VOP_STATUS & 1u) && t; t--)
                        ;
            }
        }
        for (u32 t = 2000; (VOP_STATUS & 1u) && t; t--)
            ;
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            FB[y * 320 + x] = STRIP[y * w + x];
    for (int i = 0; i < 4; i++)
        CLIP[i] = ui_clip[i];
}

/* Text in RECHO's font 1, vertically centred on y_mid; nothing if the font isn't loaded. */
void ui_text(int x, int y_mid, const char *text, int maxw, u32 colour)
{
    int h = FONTS[FONT * 4 + 2];
    if (text && *text && FONTS[FONT * 4 + 3] && h > 0 && h <= 2 * y_mid + 2)
        draw_text(FONT, x, y_mid - h / 2, text, maxw, colour, -1);
}

static void draw_line(int panel_row, int rows, const char *text, u32 colour, int underline)
{
    ui_borrow(SB_W, rows);
    fill_rect(0, 0, 0, rows - 1, GREY);                  /* divider along the left edge */
    if (underline)
        fill_rect(8, rows - 2, SB_W - 9, rows - 2, GREY);
    ui_text(8, rows / 2, text, SB_W - 14, colour);
    ui_return(PANEL_X, PANEL_Y0 + panel_row, SB_W, rows);
}

static void draw_entry(u32 i)
{
    const char *text = 0;
    if (i == 0 && !sb_walk)
        text = Q_ACTIVE ? "End of queue" : "Nothing playing";
    if (sb_walk) {
        u32 cur = sb_cursor;
        int nxt = queue_next(QUEUE, cur, PLAY_MODE, 0);
        if (nxt < 0 || (u32)nxt == cur || (u32)nxt >= Q_COUNT) {
            sb_walk = 0;
            if (i == 0)
                text = "End of queue";
        } else {
            unsigned char vol = 0;
            char *buf = NAMEBUF;
            buf[0] = 0;
            if (queue_name(QSTORE, (u32)nxt, buf, &vol, 0) == 0) {
                buf[NAME_MAX - 1] = 0;
                sidebar_clean_name(buf);
                text = buf;
                sb_cursor = (u32)nxt;
            } else {
                sb_walk = 0;                         /* lookup failed: stop, never retry here */
            }
        }
    }
    if (i == 0) {
        extern void np_set_next(const char *name);   /* ui_np.c: "Next" in the player strip */
        np_set_next(text);
    }
    draw_line(HEADER_H + (int)i * LINE_H, LINE_H, text, i == 0 ? WHITE : SOFT, 0);
}

/* Header: "Up Next", the play-mode glyph (RECHO's own icons), battery % on the right. */
#define BATTERY  PTR(volatile int *, 0x030401EC)
#define RULE_C   0x4208u
#define GREEN_C  0x47E8u
#define AMBER_C  0xFD20u
#define RED_C    0xE249u

static u32 header_key(void)
{
    int pm = BATTERY[1];
    u32 pct = pm <= 0 ? 0u : (pm >= 1000 ? 100u : (u32)pm / 10u);
    u32 chg = BATTERY[3] != 0;
    return pct | (chg << 8) | ((u32)PLAY_MODE << 12) | (Q_ACTIVE ? 1u << 16 : 0u);
}

static void draw_header(u32 k)
{
    static const char MODE[5][4] = {"\xee\x80\x83", "\xee\x80\x84", "\xee\x80\x85",
                                    "\xee\x80\x86", "\xee\x80\xab"};
    u32 pct = k & 0xFFu, chg = (k >> 8) & 1u, mode = (k >> 12) & 0xFu;
    char bat[8], *p = bat;
    if (pct >= 100u)
        *p++ = '1';
    if (pct >= 10u)
        *p++ = (char)('0' + (pct / 10u) % 10u);
    *p++ = (char)('0' + pct % 10u);
    *p++ = '%';
    *p = 0;
    u32 bc = chg ? GREEN_C : (pct <= 15u ? RED_C : (pct <= 35u ? AMBER_C : WHITE));
    ui_borrow(SB_W, HEADER_H);
    fill_rect(0, 0, 0, HEADER_H - 1, GREY);
    fill_rect(0, HEADER_H - 1, SB_W - 1, HEADER_H - 1, RULE_C);
    ui_text(8, 10, "Up Next", 70, WHITE);
    int w = text_width(1, "Up Next");
    if (mode < 5u)
        ui_text(8 + w + 6, 10, MODE[mode], 20, SOFT);
    int wb = text_width(1, bat);
    ui_text(SB_W - 8 - wb, 10, bat, wb + 2, bc);
    ui_return(PANEL_X, PANEL_Y0, SB_W, HEADER_H);
}

/* Called from the POWER tick wrapper on every main-loop pass. */
void echo_sidebar_tick(void)
{
    if (!VOP_ACTIVE || BL_DUTY == 0)
        return;                                          /* screen off: stay pending */
    u32 key[6] = {Q_ACTIVE, Q_CURRENT, PLAY_MODE, Q_COUNT, Q_SEED, Q_PASS};
    u32 now = ms_now();
    int changed = sb_dirty != 0;
    for (int i = 0; i < 6; i++)
        if (key[i] != sb_key[i])
            changed = 1;
    if (changed) {                                       /* restart the walk from the current track */
        for (int i = 0; i < 6; i++)
            sb_key[i] = key[i];
        sb_cursor = key[1];
        sb_walk = key[0] && key[3] > 1;
        sb_line = 1;
        sb_changed_at = sb_dirty ? now - SETTLE_MS : now;
        sb_dirty = 0;
        return;
    }
    if (sb_blank) {
        push_idle();
        echo_blank_outside();
        sb_blank = 0;
        return;
    }
    if (!sb_line) {
        u32 hk = header_key();
        if (hk != sb_hdr_key) {
            push_idle();
            draw_header(hk);
            sb_hdr_key = hk;
        }
        return;
    }
    if (now - sb_changed_at < SETTLE_MS)
        return;
    push_idle();
    if (sb_line == 1) {
        u32 hk = header_key();
        draw_header(hk);
        sb_hdr_key = hk;
    } else
        draw_entry(sb_line - 2);
    sb_line = (sb_line > LINES) ? 0 : sb_line + 1;
}

/* Panel (re)initialised at boot or wake. */
void echo_sidebar_invalidate(void)
{
    extern void echo_band_invalidate(void);
    sb_dirty = 1;
    sb_blank = 1;
    echo_band_invalidate();
}
