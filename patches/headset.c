/*
 * Echo: buttons on a wired headset (3.5 mm remote), as FiiO's firmware reads them.
 *
 * The remote's buttons pull the 3.5 mm plug's mic contact down; the Echo reads it on ADC channel 2
 * (the Echo Mini has no such line). Bands of the 10-bit reading (FiiO 2.0.0, documented by Podium's
 * docs/hardware.md): below 16 play (the button shorts the mic to ground), 16-45 volume up, 46-100
 * volume down, above 100 nothing pressed.
 *
 * RECHO already samples channel 2: its key poll (0x30583A8, every 10 ms) reads channel 1 (the key
 * ladder) three times out of four and, every fourth time, the next channel of the table at
 * 0x3060180 ({0, 2, 3}); each reading that moves more than 24 from the last one is kept in
 * 0x300418C[channel]. The Echo build changes the table to {0, 2, 2}, so channel 2 is read every 40 or
 * 80 ms instead of 120 (channel 3 is only kept there, nothing reads it; the battery, channel 0, is
 * read as before). headset_tick (POWER tick) turns the kept reading into buttons:
 *
 *   play  1 tap: play / pause, 2 taps: next, 3 taps: previous (taps close 300 ms after the last)
 *   vol+  tap: volume up, hold 400 ms: next          vol-  tap: volume down, hold: previous
 *
 * Only with a 3.5 mm plug in and Bluetooth not playing, from any screen, without waking it. After a
 * plug goes in, whichever of "play band" and "anything else" the line holds first for 500 ms
 * decides whether it has a remote: a plug without a mic contact (3-pole) reads 0-1, the play band,
 * and is ignored. A remote that then reads play for over 1 s is taken for such a plug too, until the
 * line leaves the play band for 500 ms.
 */
typedef unsigned int u32;

static inline u32 lit(u32 v)
{
    __asm__("" : "+r"(v));
    return v;
}
#define FN(addr, type) ((type)lit((addr) | 1u))
#define STATE __attribute__((section(".text.headset_state,\"ax\",%progbits@"), used))

#define ADC_KEPT    ((const volatile u32 *)lit(0x0300418Cu))     /* RECHO: last reading per channel */
#define jacks       FN(0x03055EC0, u32 (*)(void))                /* bit 0: 3.5 mm plug in */
#define ms_now      FN(0x03051638, u32 (*)(void))
#define vol_step    FN(0x0305A464, void (*)(int dir))            /* RECHO's volume key (+1 / -1) */
#define skip        FN(0x0305AB80, void (*)(int dir))            /* next (+1) / previous (-1) track */
#define play_state  FN(0x03057BD0, int (*)(void))
#define play_toggle FN(0x03057C94, void (*)(int playing))        /* as RECHO's ▶‖ tap: arg = state == 1 */

enum { NONE, PLAY, UP, DOWN };
enum { UNKNOWN, MIC, NO_MIC };

STATE static volatile u32 hs_plug = 0, hs_mic = UNKNOWN, hs_band = NONE, hs_since = 0, hs_plug_at = 0,
                          hs_taps = 0, hs_tap_at = 0, hs_long = 0;

static u32 band(u32 v)
{
    return v < 16u ? PLAY : (v < 46u ? UP : (v <= 100u ? DOWN : NONE));
}

void headset_tick(void)
{
    extern volatile u32 bt_routed;
    u32 now = ms_now();
    if (!(jacks() & 1u) || bt_routed) {
        hs_plug = 0;
        return;
    }
    u32 b = band(ADC_KEPT[2]);
    if (!hs_plug) {                                   /* a plug just went in */
        hs_plug = 1;
        hs_plug_at = hs_since = now;
        hs_mic = UNKNOWN;
        hs_band = b;
        hs_taps = hs_long = 0;
        return;
    }
    if (b != hs_band) {                               /* a button went down or up */
        u32 old = hs_band, held = now - hs_since;
        hs_band = b;
        hs_since = now;
        if (hs_mic == MIC && b == NONE) {
            if (old == PLAY && held < 400u) {
                hs_taps++;
                hs_tap_at = now;
            } else if ((old == UP || old == DOWN) && !hs_long) {
                vol_step(old == UP ? 1 : -1);
            }
        }
        hs_long = 0;
        return;
    }
    u32 held = now - hs_since;
    if (hs_mic == UNKNOWN) {
        if (now - hs_plug_at >= 500u && held >= 500u)
            hs_mic = b == PLAY ? NO_MIC : MIC;
        return;
    }
    if (hs_mic == NO_MIC) {
        if (b != PLAY && held >= 500u)
            hs_mic = MIC;
        return;
    }
    if (b == PLAY && held > 1000u) {                  /* a plug without a mic after all */
        hs_mic = NO_MIC;
        hs_taps = 0;
        return;
    }
    if ((b == UP || b == DOWN) && held >= 400u && !hs_long) {
        hs_long = 1;
        skip(b == UP ? 1 : -1);
    }
    if (b == NONE && hs_taps && now - hs_tap_at >= 300u) {
        u32 n = hs_taps;
        hs_taps = 0;
        if (n == 1u)
            play_toggle(play_state() == 1);
        else
            skip(n == 2u ? 1 : -1);
    }
}
