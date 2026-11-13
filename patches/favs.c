/*
 * Favourites (both players): FAVORITE.M3U at the top of each drive (internal, SD card) holds that
 * drive's favourite songs.
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
 * "file:///internal/" or "file:///sd/" + the percent-encoded path. Files are read and written with RECHO's routines:
 *   0x30531F0(volume, "NAME    EXT", file)        find a file in the top folder by 8.3 name, 0 = found
 *   0x3053934(volume, file, offset, buf, len)     read, returns the bytes read
 *   0x3053BF4(volume, "NAME    EXT", buf, len)    write a whole file (RECHO saves RECHO.CFG with it)
 * The list is kept within one 4 KB cluster: the write routine then overwrites in place, the path
 * RECHO itself uses for its settings. A longer list is refused ("Favourites full") rather than
 * taking the multi-cluster path RECHO never uses this way. An SD card's list is written the same
 * way to the card (4 KB is within one cluster of any FAT32 / exFAT card). Playing the favourites
 * queues the internal list, then the SD card's (RECHO's queue keeps the drive per song).
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
#define find_name      FN(0x030638FC, int (*)(void *volume, u32 folder, const char *name, u32 len, char *rec))
#define queue_start    FN(0x0305B3AC, void *(*)(void))                 /* RECHO's queue builder */
#define queue_add      FN(0x03063F74, int (*)(void *q, u32 vol, u32 folder, const char *rec, const char *path))
#define queue_finish   FN(0x0305B3F0, int (*)(int a, const char *name))
#define queue_abandon  FN(0x0305B2FC, void (*)(void))
#define set_module     FN(0x0305B234, void (*)(int module))
#define M44_SD_FLAG    REG(0x0303AB2C)
#define Q_ACTIVE       REG(0x0303BD74)
#define Q_CURRENT      REG(0x0303BD78)
#define CURRENT_MODULE REG(0x030399C8)

#define FAV_MAX  4096u                         /* one cluster: see above */
#define FAV_BUF  PTR(char *, 0x03045000)       /* FAV_MAX + 2 bytes, free RAM (docs/08-memory.md) */
#define FAV_PATH PTR(char *, 0x03046100)       /* 256 bytes: the song's path */
#define FAV_RAW  PTR(char *, 0x03046200)       /* 512 bytes: its line from RQUEUE.M3U */
#define FAV_REC  PTR(char *, 0x03046400)       /* 276 bytes: a directory record from find_name */
#define FAV_DIR  PTR(char *, 0x03046520)       /* 256 bytes: the song's folder as "/A/B/" */
#define PATH_MAX 255u
#define RAW_MAX  511u

#define STATE __attribute__((section(".text.fav_state,\"ax\",%progbits@"), used))
STATE volatile u32 fav_is_current = 0;         /* np_draw: heart after the title */
STATE volatile u32 fav_request = 0;            /* keys.c: hold ≡ on Now Playing */
STATE volatile u32 fav_key = 0xFFFFFFFFu;      /* track + screen the flag belongs to */
STATE volatile u32 fav_play_request = 0;       /* keys.c: ▶‖ on the Favourites row in Files */
STATE volatile u32 fav_vol = 0;                /* drive of the last song_path: 0 internal, 1 SD card */

static const char PREFIX[] = "file:///internal/";
static const char PREFIX_SD[] = "file:///sd/";

/* the line starts with p (n chars) */
static int starts(const char *raw, const char *p, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (raw[i] != p[i])
            return 0;
    return 1;
}

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

/* the playing song's path from the top of its drive into FAV_PATH (drive in fav_vol); its length
 * or 0. v is the internal drive, where RECHO keeps its queue files. */
static u32 song_path(void *v)
{
    if (!Q_ACTIVE || !v)
        return 0;
    u32 file[12], entry[9];
    if (find83(v, "RQUEUE  IDX", file) != 0 || file_read(v, file, 36u + 36u * Q_CURRENT, entry, 36u) != 36)
        return 0;
    u32 off = entry[4], len = entry[5];
    if (len <= sizeof PREFIX_SD - 1u || len > RAW_MAX)
        return 0;
    char *raw = FAV_RAW;
    if (find83(v, "RQUEUE  M3U", file) != 0 || file_read(v, file, off, raw, len) != (int)len)
        return 0;
    u32 start;
    if (len > sizeof PREFIX - 1u && starts(raw, PREFIX, sizeof PREFIX - 1u)) {
        start = sizeof PREFIX - 1u;
        fav_vol = 0;
    } else if (starts(raw, PREFIX_SD, sizeof PREFIX_SD - 1u)) {
        start = sizeof PREFIX_SD - 1u;
        fav_vol = 1;
    } else {
        return 0;
    }
    char *out = FAV_PATH;
    u32 n = 0;
    for (u32 i = start; i < len && n < PATH_MAX; i++) {
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
    u32 plen = song_path(volume_of(0));
    void *v = plen ? volume_of(fav_vol) : 0;
    if (!v) {
        bar_message("Favourites: no song to add");
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

/* Play FAVORITE.M3U: each line is walked folder by folder with RECHO's name lookup and added to
 * a new queue with the routine Files uses for a folder (m44 0x307A278 -> 0x3063F74); RECHO then
 * finishes the queue, starts playing and Now Playing opens, as after holding ▶‖ on a folder. */
__attribute__((noinline)) static void play_list(void)
{
    void *q = 0;
    u32 added = 0, any = 0;
    for (u32 vol = 0; vol < 2u; vol++) {
    void *v = volume_of(vol);
    u32 n = v ? list_load(v) : 0;
    if (n > FAV_MAX)
        n = FAV_MAX;
    if (!n)
        continue;
    any = 1;
    if (!q) {
        M44_SD_FLAG = vol;                     /* the drive, as Files sets it */
        q = queue_start();
        if (!q)
            return;                            /* RECHO showed why */
    }
    const char *b = FAV_BUF;
    for (u32 i = 0; i < n;) {
        u32 e = i;
        while (e < n && b[e] != '\n')
            e++;
        u32 l = e;
        if (l > i && b[l - 1] == '\r')
            l--;
        if (l > i && b[i] != '#') {            /* a song: "A/B/Song.flac" */
            char *dir = FAV_DIR;
            u32 d = 0, folder = 0, s = i, ok = 1;
            dir[d++] = '/';
            for (u32 k = i; k <= l && ok; k++) {
                if (k < l && b[k] != '/' && b[k] != '\\')
                    continue;
                u32 len = k - s;
                if (!len || len > 250u) {
                    ok = 0;
                    break;
                }
                char *name = FAV_PATH;          /* one path part, NUL-terminated */
                for (u32 j = 0; j < len; j++)
                    name[j] = b[s + j];
                name[len] = 0;
                if (find_name(v, folder, name, len, FAV_REC) != 0) {
                    ok = 0;
                    break;
                }
                if (k < l) {                    /* a folder: go in */
                    folder = *(const u32 *)(FAV_REC + 260);
                    for (u32 j = 0; j < len && d < 250u; j++)
                        dir[d++] = name[j];
                    dir[d++] = '/';
                }
                s = k + 1u;
            }
            dir[d] = 0;
            if (ok && queue_add(q, vol, folder, FAV_REC, dir) == 0)
                added++;
        }
        i = e + 1u;
    }
    }
    if (!any) {
        bar_message("No favourites yet: hold \xe2\x89\xa1 on Now Playing");
        return;
    }
    if (!added) {
        queue_abandon();
        bar_message("Favourites: no songs found");
        return;
    }
    if (queue_finish(-1, "Favourites") >= 0)
        set_module(45);
}

/* POWER tick: hold ≡ requests, and the heart for the song on Now Playing */
void fav_tick(void)
{
    if (fav_play_request) {                    /* from Files (m44), handled before the module check */
        fav_play_request = 0;
        play_list();
        return;
    }
    if (fav_request) {                         /* hold ≡: Now Playing, or the screen off (any screen) */
        fav_request = 0;
        toggle();
        fav_key = CURRENT_MODULE == 45u ? (Q_ACTIVE ? Q_CURRENT : 0xFFFFFFFEu) : 0xFFFFFFFFu;
        return;
    }
    if (CURRENT_MODULE != 45u) {
        fav_key = 0xFFFFFFFFu;                 /* look again when Now Playing opens */
        return;
    }
    u32 key = Q_ACTIVE ? Q_CURRENT : 0xFFFFFFFEu;
    if (key == fav_key)
        return;
    fav_key = key;
    u32 was = fav_is_current, is = 0;
    u32 plen = song_path(volume_of(0));
    void *v = plen ? volume_of(fav_vol) : 0;
    if (v) {
        u32 n = list_load(v);
        if (n > FAV_MAX)
            n = FAV_MAX;
        is = list_find(n, plen) >= 0;
    }
    fav_is_current = is;
    if (is != was)
        redraw();
}
