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
