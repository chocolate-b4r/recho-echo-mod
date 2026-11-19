/*
 * Artists (Echo Mini): the fourth row of Files' drive list, under Favourites. It lists the
 * artists of every playable song on the internal drive and the SD card, A to Z; ▶‖ on one plays
 * all of that artist's songs. The artist is the part of the file name before " - "
 * ("Halsey - Ashley.flac"); files named otherwise are not listed.
 *
 * RECHO's own routines, read only from its code (2d11bdcd):
 *   0x30535E0(volume, folder, dirstate)   open a folder (0 = the top), dirstate 1,092 bytes; < 0 error
 *   0x3053640(dirstate, rec)               next entry into a 276-byte record (+0 name, +256
 *                                          attributes, +260 first cluster); 1 = got one
 *   0x3062D14(name)                        "plays this": .flac, .wav, .mp3
 *   0x305B3AC / 0x3063F74 / 0x305B3F0 / 0x305B2FC   queue start / add / finish / abandon
 * The walk uses its own dirstate and record (free RAM), not RECHO's, and feeds the watchdog
 * (0x3051C48) as it goes. Folders: up to 31 per drive, any depth.
 */
typedef unsigned int u32;
typedef unsigned char u8;

static inline u32 lit(u32 v)
{
    __asm__("" : "+r"(v));
    return v;
}
#define FN(addr, type) ((type)lit((addr) | 1u))
#define REG(addr)      (*(volatile u32 *)lit(addr))
#define PTR(type, a)   ((type)lit(a))

#define volume_of     FN(0x0305A02C, void *(*)(u32 vol))
#define dir_open      FN(0x030535E0, int (*)(void *volume, u32 folder, void *ds))
#define dir_next      FN(0x03053640, int (*)(void *ds, char *rec))
#define plays         FN(0x03062D14, int (*)(const char *name))
#define delay_ms      FN(0x03051C48, void (*)(u32 ms))
#define queue_start   FN(0x0305B3AC, void *(*)(void))
#define queue_add     FN(0x03063F74, int (*)(void *q, u32 vol, u32 folder, const char *rec, const char *path))
#define queue_finish  FN(0x0305B3F0, int (*)(int a, const char *name))
#define queue_abandon FN(0x0305B2FC, void (*)(void))
#define set_module    FN(0x0305B234, void (*)(int module))
#define bar_message   FN(0x0305A874, void (*)(const char *text))
#define screen_redraw FN(0x0305C51C, void (*)(const void *desc))
#define panel_rect    FN(0x0305C428, const int *(*)(void))
#define draw_text     FN(0x03058D84, int (*)(int font, int x, int y, const char *s, int maxw, u32 fg, int bg))
#define fill_rect     FN(0x0306BF2C, void (*)(int, int, int, int, u32))
#define CURRENT_MODULE REG(0x030399C8)
#define M44_SD_FLAG   REG(0x0303AB2C)
#define BROWSE_DESC   ((const void *)lit(0x0307B58Cu))

#define DIRS      PTR(char *, 0x03043010)     /* 31 x 128: u32 first cluster, then "/A/B/" */
#define DIR_N     31u
#define DIR_SZ    128u
#define DS        PTR(void *, 0x03046640)     /* 1,092 bytes */
#define REC       PTR(char *, 0x03046A90)     /* 276 bytes */
#define IDX       PTR(char *, 0x03046BB0)     /* ART_MAX x ART_LEN names, up to 0x03048200 */
#define ART_MAX   158u
#define ART_LEN   36u

#define STATE __attribute__((section(".text.art_state,\"ax\",%progbits@"), used))
STATE volatile u32 art_mode = 0;              /* 1 while the artist list shows */
STATE volatile u32 art_request = 0;           /* 1 read the artists, 2 play the one under the cursor */
STATE volatile u32 art_count = 0, art_cursor = 0, art_top = 0;
STATE volatile u32 art_kind = 0;              /* 0 Artists, 1 Favourites (pick the song to start with) */
extern u32 fav_names(char *dst, u32 max, u32 len);
extern volatile int fav_play_from;
extern volatile u32 fav_play_request;
STATE static void *art_q = 0;
STATE static u32 art_added = 0;

/* letters before " - ", or 0 */
static u32 artist_len(const char *s)
{
    for (u32 i = 1; s[i] && i < 200u; i++)
        if (s[i] == ' ' && s[i + 1] == '-' && s[i + 2] == ' ')
            return i;
    return 0;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* a (n letters) against the name b, ignoring case: <0, 0, >0 */
static int compare(const char *a, u32 n, const char *b)
{
    for (u32 i = 0; i < n; i++) {
        int d = lower((u8)a[i]) - lower((u8)b[i]);
        if (d || !b[i])
            return d ? d : 1;
    }
    return b[n] ? -1 : 0;
}

static void add_artist(u32 vol, u32 folder, const char *name, u32 n)
{
    (void)vol;
    (void)folder;
    if (n >= ART_LEN)
        n = ART_LEN - 1u;
    u32 at = 0;
    for (; at < art_count; at++) {
        int c = compare(name, n, IDX + at * ART_LEN);
        if (!c)
            return;
        if (c < 0)
            break;
    }
    if (art_count >= ART_MAX)
        return;
    for (u32 k = art_count; k > at; k--)
        for (u32 j = 0; j < ART_LEN; j++)
            IDX[k * ART_LEN + j] = IDX[(k - 1u) * ART_LEN + j];
    char *d = IDX + at * ART_LEN;
    for (u32 j = 0; j < n; j++)
        d[j] = name[j];
    d[n] = 0;
    art_count++;
}

static void add_song(u32 vol, u32 folder, const char *name, u32 n)
{
    if (n >= ART_LEN)                          /* the list keeps the first 35 letters */
        n = ART_LEN - 1u;
    if (compare(name, n, IDX + art_cursor * ART_LEN) != 0)
        return;
    if (queue_add(art_q, vol, *(const u32 *)(DIRS + folder * DIR_SZ), REC, DIRS + folder * DIR_SZ + 4) == 0)
        art_added++;
}

/* every playable "Artist - Title" file on the drive, folder by folder */
static void walk(u32 vol, void (*visit)(u32 vol, u32 folder, const char *name, u32 n))
{
    void *v = volume_of(vol);
    if (!v)
        return;
    u32 ndirs = 1, seen = 0;
    *(u32 *)DIRS = 0;
    DIRS[4] = '/';
    DIRS[5] = 0;
    for (u32 i = 0; i < ndirs; i++) {
        char *dir = DIRS + i * DIR_SZ;
        if (dir_open(v, *(const u32 *)dir, DS) < 0)
            continue;
        while (dir_next(DS, REC) == 1) {
            if ((++seen & 31u) == 0)
                delay_ms(1);                  /* also feeds the watchdog */
            u8 a = (u8)REC[256];
            if (REC[0] == '.' || (a & 6u))
                continue;
            if (a & 0x10u) {                  /* a folder: remember it with its path */
                if (ndirs >= DIR_N)
                    continue;
                char *nd = DIRS + ndirs * DIR_SZ;
                u32 p = 4, ok = 1;
                for (const char *q = dir + 4; *q; q++)
                    nd[p++] = *q;
                for (const char *q = REC; *q && ok; q++) {
                    if (p >= DIR_SZ - 2u)
                        ok = 0;
                    else
                        nd[p++] = *q;
                }
                if (!ok)
                    continue;
                nd[p++] = '/';
                nd[p] = 0;
                *(u32 *)nd = *(const u32 *)(REC + 260);
                ndirs++;
                continue;
            }
            u32 n = artist_len(REC);
            if (n && plays(REC))
                visit(vol, i, REC, n);
        }
    }
}

/* POWER tick: read the artists, or play one */
void art_tick(void)
{
    if (CURRENT_MODULE != 44u) {
        art_mode = 0;
        art_request = 0;
        return;
    }
    u32 r = art_request;
    if (!r)
        return;
    art_request = 0;
    if (r == 1u) {
        art_count = art_cursor = art_top = 0;
        if (art_kind) {
            art_count = fav_names(IDX, ART_MAX, ART_LEN);
        } else {
            walk(0, add_artist);
            walk(1, add_artist);
        }
        screen_redraw(BROWSE_DESC);
        return;
    }
    if (art_cursor >= art_count)
        return;
    if (art_kind) {                           /* favs.c plays the list from this song */
        fav_play_from = (int)art_cursor;
        fav_play_request = 1;
        art_mode = 0;
        return;
    }
    M44_SD_FLAG = 0;
    art_q = queue_start();
    if (!art_q)
        return;                               /* RECHO showed why */
    art_added = 0;
    walk(0, add_song);
    walk(1, add_song);
    if (!art_added) {
        queue_abandon();
        bar_message("No songs found");
        return;
    }
    if (queue_finish(-1, IDX + art_cursor * ART_LEN) >= 0) {
        art_mode = 0;
        set_module(45);
    }
}

/* the list, over RECHO's drive list (files_draw calls it while art_mode is set) */
void art_draw(void)
{
    const int *rc = panel_rect();
    int y0 = rc[1] + 22, rows = (rc[3] - y0 + 1) / 20;
    if (rows < 1)
        rows = 1;
    fill_rect(0, y0, 319, rc[3], 0);
    if (art_request == 1u || (!art_count && art_request)) {
        draw_text(1, 12, y0 + 2, "Reading...", 280, 0x8410u, -1);
        return;
    }
    if (!art_count) {
        draw_text(1, 12, y0 + 2, art_kind ? "No favourites yet" : "No \"Artist - Title\" songs", 280, 0x8410u, -1);
        return;
    }
    if (art_cursor < art_top)
        art_top = art_cursor;
    if (art_cursor >= art_top + (u32)rows)
        art_top = art_cursor - (u32)rows + 1u;
    for (int i = 0; i < rows && art_top + (u32)i < art_count; i++) {
        u32 k = art_top + (u32)i;
        int y = y0 + 20 * i;
        u32 fg = 0xF79Eu;
        if (k == art_cursor) {
            fill_rect(8, y, 311, y + 19, 0x24DFu);
            fg = 0;
        }
        draw_text(1, 12, y + 2, IDX + k * ART_LEN, 280, fg, -1);
    }
}

/* keys while the list shows: all of them stay here */
int art_key(const unsigned char *ev, int dir)
{
    if (ev[0] == 6) {                          /* ↩: back to the drive list */
        if (ev[1] == 2u) {
            art_mode = 0;
            screen_redraw(BROWSE_DESC);
        }
        return 1;
    }
    if (ev[0] == 4) {                          /* ▶‖: play this artist */
        if ((ev[1] == 2u || ev[1] == 3u) && art_count)
            art_request = 2;
        return 1;
    }
    if (dir && (ev[1] == 1u || ev[1] == 4u) && art_count) {
        u32 c = art_cursor;                    /* press: one row; held (repeat): the next letter */
        int first = lower((u8)IDX[c * ART_LEN]);
        for (u32 k = 0; k < art_count; k++) {
            c = dir > 0 ? (c + 1u < art_count ? c + 1u : 0u) : (c ? c - 1u : art_count - 1u);
            if (ev[1] == 1u || lower((u8)IDX[c * ART_LEN]) != first)
                break;
        }
        if (ev[1] == 4u && dir < 0) {          /* going up: land on the letter's first artist */
            int l = lower((u8)IDX[c * ART_LEN]);
            while (c && lower((u8)IDX[(c - 1u) * ART_LEN]) == l)
                c--;
        }
        art_cursor = c;
        screen_redraw(BROWSE_DESC);
    }
    return 1;
}

void art_open(u32 kind)
{
    art_kind = kind;
    art_mode = 1;
    art_request = 1;
    screen_redraw(BROWSE_DESC);
}
