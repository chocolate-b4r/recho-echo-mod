/*
 * RECHO on the full-size Echo — the message line under the screen (stage 7).
 *
 * Button bar: removed on the Echo (keys are learnt once; see docs). RECHO's own bar
 * messages are shown on one line at panel rows 245..270, under the centred screen.
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

#define BAR_Y       245
#define BAR_H       26
#define MSG_W       320

#define WHITE 0xF79Eu
#define SOFT  0xBDD7u
#define GREY  0x8410u
#define RULE  0x4208u
#define AMBER 0xFD20u


#define STATE __attribute__((section(".text.band_state,\"ax\",%progbits@"), used))
STATE volatile u32 bar_shown_sum = 0xFFFFFFFFu;      /* checksum of the bar on the panel; 0 = blank */

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
    if (hints_dirty) {
        hints_dirty = 0;
        push_idle();
        draw_bar();
    }
}

void echo_band_invalidate(void)
{
    extern void clock_invalidate(void);
    clock_invalidate();
    bar_shown_sum = 0xFFFFFFFFu;
    hints_dirty = 1;
}
