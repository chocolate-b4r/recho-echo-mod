/*
 * Favourites (both players): FAVORITE.M3U at the top of the internal drive.
 *
 * Hold ≡ on Now Playing adds the song playing to the list or takes it off; a heart after the title
 * shows it is in the list. The file is a plain playlist that phones and computers read:
 *
 *   #EXTM3U
 *   Halsey - Ashley.flac
 *   Music/Album/Song.flac
 *
 * one song per line, as its path from the top of the drive (UTF-8, '/' between folders). Copy the
 * music with the same folders plus this file to a phone and players such as Poweramp, Musicolet,
 * VLC or AIMP open it as a playlist. Firmware updates write only the firmware area, so it stays.
 *
 * The song's path comes from RECHO's own queue: RQUEUE.IDX has a 36-byte header, then one 36-byte
 * entry per song with the offset (+16) and length (+20) of its line in RQUEUE.M3U, which is
 * "file:///internal/" + the percent-encoded path. Files are read and written with RECHO's routines:
 *   0x30531F0(volume, "NAME    EXT", file)        find a file in the top folder by 8.3 name, 0 = found
 *   0x3053934(volume, file, offset, buf, len)     read, returns the bytes read
 *   0x3053BF4(volume, "NAME    EXT", buf, len)    write a whole file (RECHO saves RECHO.CFG with it)
 * The list is kept within one 4 KB cluster: the write routine then overwrites in place, the path
 * RECHO itself uses for its settings. A longer list is refused ("Favourites full") rather than
 * taking the multi-cluster path RECHO never uses this way. Songs on an SD card are not listed.
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

#define volume_of      FN(0x0305A02C, void *(*)(u32 vol))
#define find83         FN(0x030531F0, int (*)(void *volume, const char *name83, void *file))
#define file_read      FN(0x03053934, int (*)(void *volume, void *file, u32 offset, void *buf, u32 len))
#define file_write     FN(0x03053BF4, int (*)(void *volume, const char *name83, const void *buf, u32 len))
#define bar_message    FN(0x0305A874, void (*)(const char *text))
#define current_screen FN(0x0305C41C, const volatile u32 *(*)(void))
#define request_redraw FN(0x0305C51C, void (*)(const void *screen))
#define Q_ACTIVE       REG(0x0303BD74)
#define Q_CURRENT      REG(0x0303BD78)
#define CURRENT_MODULE REG(0x030399C8)

#define FAV_MAX  4096u                         /* one cluster: see above */
#define FAV_BUF  PTR(char *, 0x03045000)       /* FAV_MAX + 2 bytes, free RAM (docs/08-memory.md) */
#define FAV_PATH PTR(char *, 0x03046100)       /* 256 bytes: the song's path */
#define FAV_RAW  PTR(char *, 0x03046200)       /* 512 bytes: its line from RQUEUE.M3U */
#define PATH_MAX 255u
#define RAW_MAX  511u

#define STATE __attribute__((section(".text.fav_state,\"ax\",%progbits@"), used))
STATE volatile u32 fav_is_current = 0;         /* np_draw: heart after the title */
STATE volatile u32 fav_request = 0;            /* keys.c: hold ≡ on Now Playing */
STATE volatile u32 fav_key = 0xFFFFFFFFu;      /* track + screen the flag belongs to */

static const char PREFIX[] = "file:///internal/";

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/* the playing song's path from the top of the internal drive into FAV_PATH; its length or 0 */
static u32 song_path(void *v)
{
    if (!Q_ACTIVE || !v)
        return 0;
    u32 file[12], entry[9];
    if (find83(v, "RQUEUE  IDX", file) != 0 || file_read(v, file, 36u + 36u * Q_CURRENT, entry, 36u) != 36)
        return 0;
    u32 off = entry[4], len = entry[5];
    if (len <= sizeof PREFIX - 1u || len > RAW_MAX)
        return 0;
    char *raw = FAV_RAW;
    if (find83(v, "RQUEUE  M3U", file) != 0 || file_read(v, file, off, raw, len) != (int)len)
        return 0;
    for (u32 i = 0; i < sizeof PREFIX - 1u; i++)
        if (raw[i] != PREFIX[i])
            return 0;                          /* not on the internal drive */
    char *out = FAV_PATH;
    u32 n = 0;
    for (u32 i = sizeof PREFIX - 1u; i < len && n < PATH_MAX; i++) {
        char c = raw[i];
        if (c == '\r' || c == '\n')
            break;
        if (c == '%' && i + 2u < len && hexval(raw[i + 1]) >= 0 && hexval(raw[i + 2]) >= 0) {
            c = (char)(hexval(raw[i + 1]) * 16 + hexval(raw[i + 2]));
            i += 2u;
        }
        out[n++] = c;
    }
    out[n] = 0;
    return n;
}

/* FAVORITE.M3U into FAV_BUF: its length (FAV_MAX + 1 = longer than we edit), 0 if there is none */
static u32 list_load(void *v)
{
    u32 file[12];
    if (find83(v, "FAVORITEM3U", file) != 0)
        return 0;
    int got = file_read(v, file, 0, FAV_BUF, FAV_MAX + 1u);
    if (got < 0)
        got = 0;
    FAV_BUF[got] = 0;
    return (u32)got;
}

/* where the path's line starts in FAV_BUF, or -1 */
static int list_find(u32 n, u32 plen)
{
    const char *b = FAV_BUF, *p = FAV_PATH;
    u32 i = 0;
    while (i < n) {
        u32 e = i;
        while (e < n && b[e] != '\n')
            e++;
        u32 l = e;
        if (l > i && b[l - 1] == '\r')
            l--;
        if (l - i == plen) {
            u32 k = 0;
            while (k < plen && b[i + k] == p[k])
                k++;
            if (k == plen)
                return (int)i;
        }
        i = e + 1u;
    }
    return -1;
}

static void redraw(void)
{
    const volatile u32 *scr = current_screen();
    if (scr)
        request_redraw((const void *)scr);
}

__attribute__((noinline)) static void toggle(void)   /* the only file write: verifier checks it */
{
    void *v = volume_of(0);
    u32 plen = song_path(v);
    if (!plen) {
        bar_message("Favourites: internal songs only");
        return;
    }
    u32 n = list_load(v);
    if (n > FAV_MAX) {
        bar_message("Favourites: list too long to edit");
        return;
    }
    char *b = FAV_BUF;
    int at = list_find(n, plen);
    const char *done;
    if (at >= 0) {                             /* take the line out */
        u32 e = (u32)at;
        while (e < n && b[e] != '\n')
            e++;
        if (e < n)
            e++;
        for (u32 i = e; i <= n; i++)
            b[(u32)at + i - e] = b[i];
        n -= e - (u32)at;
        done = "Removed from favourites";
    } else {                                   /* add it at the end */
        static const char HEAD[] = "#EXTM3U\n";
        if (n == 0)
            for (u32 i = 0; i < sizeof HEAD - 1u; i++)
                b[n++] = HEAD[i];
        u32 need = (n && b[n - 1] != '\n') ? 1u : 0u;
        if (n + need + plen + 1u > FAV_MAX) {
            bar_message("Favourites full");
            return;
        }
        if (need)
            b[n++] = '\n';
        for (u32 i = 0; i < plen; i++)
            b[n++] = FAV_PATH[i];
        b[n++] = '\n';
        done = "Added to favourites";
    }
    if (file_write(v, "FAVORITEM3U", b, n) != 0) {
        bar_message("Favourites: not saved");
        return;
    }
    fav_is_current = at < 0;
    bar_message(done);
    redraw();
}

/* POWER tick: hold ≡ requests, and the heart for the song on Now Playing */
void fav_tick(void)
{
    if (CURRENT_MODULE != 45u) {
        fav_request = 0;
        fav_key = 0xFFFFFFFFu;                 /* look again when Now Playing opens */
        return;
    }
    if (fav_request) {
        fav_request = 0;
        toggle();
        fav_key = Q_ACTIVE ? Q_CURRENT : 0xFFFFFFFEu;
        return;
    }
    u32 key = Q_ACTIVE ? Q_CURRENT : 0xFFFFFFFEu;
    if (key == fav_key)
        return;
    fav_key = key;
    u32 was = fav_is_current, is = 0;
    void *v = volume_of(0);
    u32 plen = song_path(v);
    if (plen) {
        u32 n = list_load(v);
        if (n > FAV_MAX)
            n = FAV_MAX;
        is = list_find(n, plen) >= 0;
    }
    fav_is_current = is;
    if (is != was)
        redraw();
}
