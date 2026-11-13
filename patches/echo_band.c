/*
 * RECHO on the full-size Echo — the player strip under RECHO's picture (stage 10).
 *
 * Button bar: removed (keys are learnt once; see docs). Panel rows 219..270 under RECHO's
 * 320 x 170 picture show a player strip; RECHO's own bar messages take its first row.
 *
 * The clock lives in clock.c (shared with the Echo Mini).
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
#define bar_message    FN(0x0305AE28, const char *(*)(void))

#define VOP_ACTIVE  REG(0x030089EC)
#define BL_DUTY     REG(0x03006174)
#define FB          PTR(volatile u16 *, 0x03008A00)

#define STRIP_Y     219
#define STRIP_ROW_H 26

#define WHITE 0xF79Eu
#define SOFT  0xBDD7u
#define GREY  0x8410u
#define RULE  0x4208u
#define AMBER 0xFD20u


#define STATE __attribute__((section(".text.band_state,\"ax\",%progbits@"), used))

extern volatile u32 sb_blank;
extern volatile u32 hints_dirty;
extern void ui_borrow(int w, int h);
extern void ui_return(int px, int py, int w, int h);
extern void ui_text(int x, int y_mid, const char *text, int maxw, u32 colour);
extern void echo_panel_fill(u32 x0, u32 y0, u32 x1, u32 y1, u32 colour);

/* ---- clock ---------------------------------------------------------------------- */

/* ---- button bar ------------------------------------------------------------------ */

/* RECHO's bar messages ("headphones out: paused", volume limits, ...) get one centred
 * line under the screen; nothing is shown otherwise (no button labels on the Echo). */
/* Player strip: panel rows 219..270 under RECHO's picture, two 320 x 26 rows drawn by
 * footer_row() (ui_np.c). A message from RECHO ("headphones out: paused", ...) replaces the
 * first row while it is up. Redrawn when anything on it changes, at most once a second. */
#define CURRENT_MODULE REG(0x030399C8)
#define player_state   FN(0x03057BD0, int (*)(void))
#define elapsed_ms     FN(0x03057BE4, u32 (*)(void))
#define queue_index    FN(0x0305A1B0, int (*)(void))
STATE volatile u32 strip_key = 0xFFFFFFFFu;
STATE volatile u32 band_lyric_shown = 0xFFFFFFFFu;
#define current_screen_b FN(0x0305C41C, const void *(*)(void))
#define request_redraw_b FN(0x0305C51C, void (*)(const void *screen))

extern void footer_row(int row, int on_now_playing);
extern void lyrics_tick(void);
extern int lyrics_index(void);

static u32 message_hash(const char *msg)
{
    u32 sum = 0;
    if (msg && *msg) {
        sum = 0x811C9DC5u;
        for (const char *p = msg; *p; p++)
            sum = (sum ^ (u8)*p) * 16777619u;
    }
    return sum;
}

static void draw_strip(const char *msg, int on_np)
{
    for (int row = 0; row < 2; row++) {
        ui_borrow(320, STRIP_ROW_H);
        if (row == (on_np ? 1 : 0))         /* Now Playing: the line right above the progress bar */
            fill_rect(8, 0, 311, 0, RULE);
        if (row == 0 && msg && *msg) {
            int w = text_width(1, msg);
            if (w > 300)
                w = 300;
            ui_text((320 - w) / 2, STRIP_ROW_H / 2, msg, 300, AMBER);
        } else {
            footer_row(row, on_np);
        }
        ui_return(0, STRIP_Y + row * STRIP_ROW_H, 320, STRIP_ROW_H);
    }
}

/* ---- tick ---------------------------------------------------------------------------- */

void echo_band_tick(void)
{
    if (!VOP_ACTIVE || BL_DUTY == 0 || sb_blank)
        return;
    lyrics_tick();                       /* reads the track's .lrc once, only while the screen is on */
    const char *msg = bar_message();
    int on_np = CURRENT_MODULE == 45u;
    {
        int li = lyrics_index();
        if (on_np && (u32)li != band_lyric_shown) {   /* the picture shows the current lyric */
            const void *scr = current_screen_b();
            if (scr)
                request_redraw_b(scr);
        }
        band_lyric_shown = (u32)li;
    }
    int state = player_state();
    u32 key = message_hash(msg) ^ ((u32)on_np << 31) ^ ((u32)state << 28) ^ ((u32)queue_index() << 16)
              ^ (state == 1 ? elapsed_ms() / 1000u : 0u);
    if (key == strip_key && !hints_dirty)
        return;
    hints_dirty = 0;
    strip_key = key;
    push_idle();
    draw_strip(msg, on_np);
}

void echo_band_invalidate(void)
{
    extern void clock_invalidate(void);
    clock_invalidate();
    strip_key = 0xFFFFFFFFu;
    hints_dirty = 1;
}
