/*
 * Lyrics (full-size Echo): synced .lrc lyrics in the player strip under RECHO's picture.
 *
 * For "Song.flac" the file is "Song.lrc" in the same folder (any case). It is read once per
 * track, while the screen is on, into free RAM (0x03048400, 7,680 bytes; longer files are cut).
 * Lines are "[mm:ss.xx]text"; several time tags on one line, "[offset:+/-ms]" and enhanced
 * word tags "<mm:ss.xx>" are understood; other tags ([ar:], [ti:], ...) are skipped.
 *
 * RECHO's own routines, read only from its code (2d11bdcd):
 *   0x3064B64(store, entry, rec, &vol, &folder)   queue entry -> 276-byte record (+0 name,
 *                                                  +256 attributes, +260 cluster, +264 size),
 *                                                  its volume number and its folder's cluster
 *   0x305A02C(vol)                                 volume number -> volume
 *   0x30638FC(volume, folder, name, len, rec)      find a name in a folder (long names,
 *                                                  case-insensitive), 0 = found
 *   0x3053900(rec, file)                           record -> 36-byte file object
 *   0x3053934(volume, file, offset, buf, len)      read, returns the bytes read
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

#define queue_name  FN(0x03064B64, int (*)(void *store, u32 entry, char *rec, u8 *vol, u32 *folder))
#define volume_of   FN(0x0305A02C, void *(*)(u32 vol))
#define find_name   FN(0x030638FC, int (*)(void *volume, u32 folder, const char *name, u32 len, char *rec))
#define file_init   FN(0x03053900, void (*)(const char *rec, u8 *file))
#define file_read   FN(0x03053934, int (*)(void *volume, u8 *file, u32 offset, char *buf, u32 len))
#define elapsed_ms  FN(0x03057BE4, u32 (*)(void))
#define file_name   FN(0x0305A1CC, const char *(*)(void))

#define QSTORE      PTR(void *, 0x0303BD80)
#define Q_ACTIVE    REG(0x0303BD74)
#define Q_CURRENT   REG(0x0303BD78)
#define LRC_BUF     PTR(char *, 0x03048400)       /* free RAM, see docs/08-memory.md */
#define LRC_CAP     7680u

#define LINES_MAX   200
#define TEXT_MAX    160

#define STATE __attribute__((section(".text.lrc_state,\"ax\",%progbits@"), used))
STATE volatile u32 lrc_ident = 0xFFFFFFFFu;   /* track the lyrics belong to */
STATE volatile u32 lrc_count = 0;             /* timed lines (0 = no lyrics) */
STATE volatile int lrc_offset = 0;            /* [offset:] in ms */
STATE u32 lrc_time[LINES_MAX] = {0};
STATE u16 lrc_text[LINES_MAX] = {0};
STATE char lrc_rec[276] = {0};
STATE u8 lrc_file[36] = {0};
STATE char lrc_name[260] = {0};
STATE char lrc_line[TEXT_MAX] = {0};

static u32 ident_now(void)
{
    u32 h = 0x811C9DC5u ^ Q_CURRENT;
    const char *n = file_name();
    for (u32 i = 0; n && n[i] && i < 255; i++)
        h = (h ^ (u8)n[i]) * 16777619u;
    return h;
}

static int digit(char c)
{
    return c >= '0' && c <= '9';
}

/* "[mm:ss.xx]" at p: time in ms, returns the length of the tag, 0 if not a time tag */
static u32 time_tag(const char *p, const char *end, u32 *ms)
{
    const char *q = p + 1;
    u32 m = 0, s = 0, f = 0, fd = 0;
    if (q >= end || !digit(*q))
        return 0;
    while (q < end && digit(*q))
        m = m * 10u + (u32)(*q++ - '0');
    if (q >= end || *q++ != ':')
        return 0;
    if (q >= end || !digit(*q))
        return 0;
    while (q < end && digit(*q))
        s = s * 10u + (u32)(*q++ - '0');
    if (q < end && (*q == '.' || *q == ':')) {
        q++;
        while (q < end && digit(*q)) {
            if (fd < 3) {
                f = f * 10u + (u32)(*q - '0');
                fd++;
            }
            q++;
        }
    }
    if (q >= end || *q != ']')
        return 0;
    while (fd < 3) {
        f *= 10u;
        fd++;
    }
    *ms = (m * 60u + s) * 1000u + f;
    return (u32)(q + 1 - p);
}

static void parse(char *b, u32 n)
{
    char *p = b, *end = b + n;
    if (n >= 3 && (u8)b[0] == 0xEF && (u8)b[1] == 0xBB && (u8)b[2] == 0xBF)
        p += 3;
    while (p < end && lrc_count < LINES_MAX) {
        char *eol = p;
        while (eol < end && *eol != '\n' && *eol != '\r')
            eol++;
        u32 times[8], nt = 0, ms;
        char *t = p;
        while (t < eol && *t == '[') {
            u32 len = time_tag(t, eol, &ms);
            if (len) {
                if (nt < 8)
                    times[nt++] = ms;
                t += len;
                continue;
            }
            /* [offset:+500] */
            if (eol - t > 8 && t[1] == 'o' && t[2] == 'f' && t[3] == 'f' && t[4] == 's' && t[5] == 'e'
                && t[6] == 't' && t[7] == ':') {
                char *q = t + 8;
                int sign = 1, v = 0;
                if (*q == '-' || *q == '+')
                    sign = *q++ == '-' ? -1 : 1;
                while (q < eol && digit(*q))
                    v = v * 10 + (*q++ - '0');
                lrc_offset = sign * v;
            }
            break;                                 /* any other tag: skip the line */
        }
        while (t < eol && *t == ' ')
            t++;
        for (u32 i = 0; i < nt && lrc_count < LINES_MAX; i++) {
            lrc_time[lrc_count] = times[i];
            lrc_text[lrc_count] = (u16)(t - b);
            lrc_count++;
        }
        p = eol;
        while (p < end && (*p == '\n' || *p == '\r'))
            p++;
    }
    /* sort by time (insertion sort, stable) */
    for (u32 i = 1; i < lrc_count; i++) {
        u32 tm = lrc_time[i];
        u16 tx = lrc_text[i];
        u32 j = i;
        while (j > 0 && lrc_time[j - 1] > tm) {
            lrc_time[j] = lrc_time[j - 1];
            lrc_text[j] = lrc_text[j - 1];
            j--;
        }
        lrc_time[j] = tm;
        lrc_text[j] = tx;
    }
}

static void load(void)
{
    lrc_count = 0;
    lrc_offset = 0;
    if (!Q_ACTIVE)
        return;
    u8 vol = 0;
    u32 folder = 0;
    if (queue_name(QSTORE, Q_CURRENT, lrc_rec, &vol, &folder) != 0)
        return;
    lrc_rec[255] = 0;
    u32 n = 0, dot = 0;
    for (; lrc_rec[n] && n < 250; n++) {
        lrc_name[n] = lrc_rec[n];
        if (lrc_rec[n] == '.')
            dot = n;
    }
    if (!dot)
        dot = n;
    lrc_name[dot] = '.';
    lrc_name[dot + 1] = 'l';
    lrc_name[dot + 2] = 'r';
    lrc_name[dot + 3] = 'c';
    lrc_name[dot + 4] = 0;
    void *volume = volume_of(vol);
    if (!volume || find_name(volume, folder, lrc_name, dot + 4, lrc_rec) != 0)
        return;
    if ((u8)lrc_rec[256] & 0x10u)                /* a folder */
        return;
    u32 size = *(const u32 *)(lrc_rec + 264);
    if (!size)
        return;
    if (size > LRC_CAP - 1)
        size = LRC_CAP - 1;
    file_init(lrc_rec, lrc_file);
    int got = file_read(volume, lrc_file, 0, LRC_BUF, size);
    if (got <= 0)
        return;
    LRC_BUF[got] = 0;
    parse(LRC_BUF, (u32)got);
}

/* Call while the strip is on screen: loads the lyrics when the track changes. */
void lyrics_tick(void)
{
    u32 id = Q_ACTIVE ? ident_now() : 0u;
    if (id == lrc_ident)
        return;
    lrc_ident = id;
    load();
}

/* index of the line being sung, or -1 */
int lyrics_index(void)
{
    if (!lrc_count)
        return -1;
    int t = (int)elapsed_ms() + lrc_offset;
    int k = -1;
    for (u32 i = 0; i < lrc_count && (int)lrc_time[i] <= t; i++)
        k = (int)i;
    return k;
}

/* the line being sung, without word tags, or 0 (no lyrics, not started, or an empty line) */
const char *lyrics_line(void)
{
    int k = lyrics_index();
    if (k < 0)
        return 0;
    const char *s = LRC_BUF + lrc_text[k];
    u32 o = 0;
    while (*s && *s != '\n' && *s != '\r' && o < TEXT_MAX - 1) {
        if (*s == '<') {                           /* <mm:ss.xx> word tag */
            const char *q = s + 1;
            while (*q && *q != '>' && *q != '\n' && *q != '\r' && q - s < 12)
                q++;
            if (*q == '>') {
                s = q + 1;
                continue;
            }
        }
        lrc_line[o++] = *s++;
    }
    while (o && lrc_line[o - 1] == ' ')
        o--;
    lrc_line[o] = 0;
    return o ? lrc_line : 0;
}
