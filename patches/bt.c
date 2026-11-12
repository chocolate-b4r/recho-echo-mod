/*
 * Bluetooth (both players) - step 2b: a Settings row and a Bluetooth screen.
 *
 *   Main menu -> Bluetooth opens the Bluetooth screen:
 *     Bluetooth           On / Off       powers the module and opens its serial line
 *     Search / Stop / Disconnect         30 s scan ("CS00"), stop ("CP00"), disconnect ("CT00")
 *     one row per device found           select = connect ("CL06" + address, "CH06" 4 s later)
 *   Vol keys move, Play selects, Back or Menu returns to the main menu.
 *   Full-size Echo (step 3): while connected, playing and nothing is plugged into a jack, the
 *   music goes to the headphones (bt_route): DAC mute GPIO2.1 high, wired output GPIO2.14 low,
 *   stock's start-up register settings once, then "CY0" 0x1E. Headphone buttons skip and pause.
 *   The Echo Mini has no routing yet (its pins are not known), so it connects but stays silent.
 *   The module is off at every start-up and is switched off before the charging screen.
 *
 * Protocol and hardware from Podium 4.6.0 (platform/device/logic/bt_module.c and
 * board/bluetooth.c), confirmed on both players on 2026-11-12 (Mini "CV13", Echo "CV15",
 * "CQIOK", "CKKK" answers to the scan):
 *   module power    GPIO2.7 (high = on)
 *   serial          UART1 at 0x400B0000, pins GPIO2.16 / GPIO2.17 (function 1), group A
 *                   (GRF 0x500100BC = 0x02000000), 115,200 8N1 from the 24 MHz crystal
 *   frames          "C" + letter + 2 chars (+ payload). Sizes follow Podium's frame_size().
 *
 * The screen is a RECHO screen descriptor (as the cable chooser's): +24 enter, +28 key,
 * +32 tick, +36 draw, +40 leave, +44 soft keys. It opens the way the chooser does:
 * 0x305BA54 closes the loaded module, 0x305C3E4 switches to the descriptor. Leaving asks
 * for module 48 (Settings) with 0x305B234, which the main loop loads as usual.
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

#define gpio_set       FN(0x03051590, void (*)(u32 bank, u32 pin, u32 level))
#define gpio_function  FN(0x03051570, void (*)(u32 bank, u32 pin, u32 function))
#define ms_now         FN(0x03051638, u32 (*)(void))
#define delay_ms       FN(0x03051C48, void (*)(u32 ms))
#define log_printf     FN(0x0306C9EC, void (*)(const char *fmt, ...))
#define draw_text      FN(0x03058D84, int (*)(int font, int x, int y, const char *s, int maxw, u32 fg, int bg))
#define text_width     FN(0x03058D20, int (*)(int font, const char *s))
#define fill_rect      FN(0x0306BF2C, void (*)(int, int, int, int, u32))
#define current_screen FN(0x0305C41C, const void *(*)(void))
#define request_redraw FN(0x0305C51C, void (*)(const void *screen))
#define switch_screen  FN(0x0305C3E4, void (*)(const void *screen))
#define close_module   FN(0x0305BA54, void (*)(void))
#define go_main_menu   FN(0x0305BA3C, void (*)(void))
#define list_keys      FN(0x03059FE8, int (*)(volatile u32 *list, const u8 *key))

#define LOG_TO_FILE    (((volatile u8 *)lit(0x0303EDD0u))[5])   /* settings block byte 5 */
#define FONTS          PTR(volatile int *, 0x03034B48)

#define UART_DATA     REG(0x400B0000)
#define UART_IER_DLH  REG(0x400B0004)
#define UART_LCR      REG(0x400B000C)
#define UART_MCR      REG(0x400B0010)
#define UART_LSR      REG(0x400B0014)
#define UART_USR      REG(0x400B007C)
#define UART_SRR      REG(0x400B0088)
#define UART_SFE      REG(0x400B0098)
#define UART_SRT      REG(0x400B009C)
#define UART_STET     REG(0x400B00A0)
#define UART_DMASA    REG(0x400B00A8)
#define GRF_IOMUX_CON REG(0x500100BC)
#define CRU_UART_CLK  REG(0x40180038)
#define CRU_GATE_1    REG(0x40180084)
#define CRU_GATE_8    REG(0x401800A0)
#define CRU_RESET_0   REG(0x401800C0)

enum { OFF, STARTING, NO_REPLY, READY, SCANNING, CONNECTING, CONNECTED };

#define READY_WAIT_MS   5000u
#define SCAN_MS         30000u
#define RETRY_MS        4000u
#define CONNECT_MS      30000u
#define QUERY_MS        3000u
#define QUERY_FAST_MS   1000u
#define RX_MAX          64
#define DEV_MAX         8
#define NAME_MAX        28

#define W 320
#define BLACK 0x0000u
#define WHITE 0xF79Eu
#define SOFT  0xBDD7u
#define GREY  0x8410u
#define DIM   0x52AAu
#define RULE  0x4208u
#define SELBG 0x2945u
#define GREEN 0x47E8u
#define AMBER 0xFD20u
#define RED   0xE249u

#define HEADER_H 22
#define ROW_H    24
#define ROWS     5                 /* rows 22..141; hint line at 156 */

#define STATE __attribute__((section(".text.bt_state,\"ax\",%progbits@"), used))
STATE volatile u32 bt_state = OFF;
STATE volatile u32 bt_since = 0;          /* start of the current state */
STATE volatile u32 bt_last_query = 0;
STATE volatile u32 bt_last_rx = 0;
STATE volatile u32 bt_answered = 0;       /* a non-status frame since the last command */
STATE volatile u32 bt_repeated = 0;
STATE volatile u32 bt_version = 0;
STATE volatile u32 bt_paired = 0;
STATE volatile u32 bt_failed = 0;         /* last connect failed */
STATE volatile u32 bt_open_pending = 0;
STATE volatile u32 bt_dirty = 0;
STATE volatile u32 bt_drawn_s = 0;
STATE volatile u32 bt_ndev = 0;
STATE volatile u32 bt_target = 0;         /* device index being connected */
STATE volatile u32 bt_rx_len = 0;
STATE volatile u32 bt_list[2] = {1, 0};   /* RECHO list: [0] count, [1] cursor */
STATE volatile u32 bt_top = 0;
STATE u8 bt_rx[RX_MAX] = {0};
STATE u8 bt_addr[DEV_MAX][6] = {{0}};
STATE char bt_name[DEV_MAX][NAME_MAX] = {{0}};
STATE char bt_conn_name[NAME_MAX] = {0};
STATE volatile u32 bt_streamed = 0;       /* we asked the module to stream */
STATE volatile u32 bt_play_state = 0;     /* module's view: 0 started, 1 paused, 2 playing */
STATE volatile u32 bt_pending_at = 0;     /* send the stream frame after a play/pause notice */
STATE volatile u32 bt_pending_on = 0;
STATE volatile u32 bt_pending = 0;
STATE volatile u32 bt_hello = 0;          /* "CY0 00" owed after connecting */
STATE volatile u32 bt_wired = 0;          /* a jack was in use while playing */
STATE volatile u32 bt_changed_at = 0;     /* last stream change */
STATE volatile u32 bt_quiet_until = 0;    /* no status queries until (after a play report) */
STATE volatile u32 bt_buds_paused = 0;
STATE volatile u32 bt_remote = 0;         /* 1 prev, 2 next, 3 pause, 4 play: handled in the tick */

/* ---- module and serial line ------------------------------------------------ */

static void pins_off(void)
{
    gpio_set(2, 7, 0);
    gpio_set(2, 16, 0);
    gpio_set(2, 17, 0);
}

static void uart_open(void)
{
    GRF_IOMUX_CON = 0x02000000u;          /* UART1 on group A */
    CRU_RESET_0 = 0x01000100u;            /* hold UART1 in reset */
    delay_ms(1);
    CRU_RESET_0 = 0x01000000u;
    CRU_GATE_1 = 0x20000000u;             /* UART1 clock on */
    CRU_GATE_8 = 0x02000000u;             /* UART1 bus clock on */
    CRU_UART_CLK = 0x00C00040u;           /* from the crystal */
    CRU_UART_CLK = 0x003F0000u;           /* divider 1 */
    UART_SRR = 7;
    UART_IER_DLH = 0;
    UART_DMASA = 1;
    UART_MCR = 0;
    UART_SFE = 1;
    UART_SRT = 0;
    UART_STET = 0;
    UART_LCR = 3;
    UART_LCR = 3 | 0x80;
    UART_DATA = 13;                       /* 115,200 */
    UART_IER_DLH = 0;
    UART_LCR = 3;
    UART_SRR = 6;
    gpio_function(2, 16, 1);
    gpio_function(2, 17, 1);
}

static void send_bytes(const u8 *b, u32 n)
{
    for (u32 i = 0; i < n; i++) {
        for (u32 polls = 0; (UART_USR & 2u) == 0 && polls < 100000u; polls++)
            ;
        UART_DATA = b[i];
    }
}

static void command(char letter)                  /* "C" letter "00" NUL */
{
    u8 f[5] = {'C', (u8)letter, '0', '0', 0};
    send_bytes(f, 5);
    if (letter != 'Q') {
        bt_answered = 0;
        bt_repeated = 0;
    }
}

static void address_command(char letter, u32 dev)  /* "C" letter "06" + address */
{
    u8 f[10];
    f[0] = 'C';
    f[1] = (u8)letter;
    f[2] = '0';
    f[3] = '6';
    for (u32 i = 0; i < 6; i++)
        f[4 + i] = bt_addr[dev][i];
    send_bytes(f, 10);
}

#ifdef ECHO_PORT
void bt_route(u32 on);
#endif

static void set_state(u32 s, u32 now)
{
    if (s == CONNECTED && bt_state != CONNECTED)
        bt_hello = 1;
    if (s != CONNECTED) {
        bt_streamed = 0;
        bt_pending = 0;
        bt_play_state = 0;
        bt_hello = 0;
#ifdef ECHO_PORT
        bt_route(0);
#endif
    }
    bt_state = s;
    bt_since = now;
    bt_last_query = now;
    bt_dirty = 1;
}

/* ---- frames ------------------------------------------------------------------ */

static int digit(u8 c)
{
    return c >= '0' && c <= '9';
}

/* Size of the frame at the start of bt_rx (Podium's rules), or 0 if more bytes are needed. */
static u32 frame_size(void)
{
    const u8 *b = bt_rx;
    u32 n = bt_rx_len, need;
    if (n < 2)
        return 0;
    switch (b[1]) {
    case 'S':
        if (n < 4)
            return 0;
        if (!digit(b[2]) || !digit(b[3]))
            return 4;
        need = 4u + (u32)(b[2] - '0') * 10u + (u32)(b[3] - '0');
        if (need >= 4u + 38u)
            return 4;
        return n >= need ? need : 0;
    case 'Q':
        if (n < 3)
            return 0;
        if (b[2] != 'O')
            return 3;
        if (n < 5)
            return 0;
        if (!digit(b[3]) || !digit(b[4]))
            return 5;
        need = 5u + (u32)(b[3] - '0') * 10u + (u32)(b[4] - '0');
        if (need > RX_MAX)
            return 5;
        return n >= need ? need : 0;
    case 'T':
    case 'H':
        if (n < 4)
            return 0;
        return ((b[2] == 'O' && b[3] == 'K') || (b[2] == 'E' && b[3] == 'R')) ? 4 : 2;
    case 'V':
    case 'K':
    case 'L':
        return n >= 4 ? 4 : 0;
    case 'A':
    case 'B':
    case 'P':
    case 'C':
        return 2;
    default:
        return 1;
    }
}

static void copy_name(char *dst, const u8 *src, u32 n)
{
    u32 k = 0;
    for (; k < n && k < NAME_MAX - 1 && src[k]; k++)
        dst[k] = (src[k] >= 0x20 && src[k] != 0x7F) ? (char)src[k] : '?';
    dst[k] = 0;
}

static void add_device(const u8 *p, u32 n)       /* 6-byte address + name */
{
    if (n < 6)
        return;
    u32 i;
    for (i = 0; i < bt_ndev; i++) {
        u32 same = 1;
        for (u32 k = 0; k < 6; k++)
            if (bt_addr[i][k] != p[k])
                same = 0;
        if (same)
            break;
    }
    if (i == bt_ndev) {
        if (bt_ndev >= DEV_MAX)
            return;
        bt_ndev++;
    }
    for (u32 k = 0; k < 6; k++)
        bt_addr[i][k] = p[k];
    copy_name(bt_name[i], p + 6, n - 6);
    if (!bt_name[i][0]) {                         /* no name: show the address */
        static const char HEXD[] = "0123456789ABCDEF";
        char *s = bt_name[i];
        for (u32 k = 0; k < 6; k++) {
            *s++ = HEXD[p[k] >> 4];
            *s++ = HEXD[p[k] & 15];
            if (k < 5)
                *s++ = ':';
        }
        *s = 0;
    }
    if (LOG_TO_FILE)
        log_printf("bt: found %02x:%02x:%02x:%02x:%02x:%02x \"%s\"", p[0], p[1], p[2], p[3], p[4], p[5],
                   bt_name[i]);
    bt_dirty = 1;
}

static void handle_frame(const u8 *b, u32 size, u32 now)
{
    if (b[1] != 'Q')
        bt_answered = 1;
    if (LOG_TO_FILE && b[1] != 'Q' && b[1] != 'S')
        log_printf("bt: rx %c%c (%u bytes, state %u)", b[1], size > 2 ? b[2] : '.', size, bt_state);
    switch (b[1]) {
    case 'V':
        if (size >= 4) {
            bt_version = b[3];
            if (bt_state == STARTING)
                set_state(READY, now);
        }
        break;
    case 'S':
        if ((size == 4 && !(b[2] == 'E' && b[3] == 'R')) || (b[2] == 'E' && b[3] == 'N')) {
            if (bt_state == SCANNING)
                set_state(READY, now);          /* scan finished */
        } else if (size > 4) {
            add_device(b + 4, size - 4);
        }
        break;
    case 'Q':
        if (size >= 3 && b[2] == 'O') {
            copy_name(bt_conn_name, b + 11, size > 11 ? size - 11 : 0);
            if (bt_state != CONNECTED) {
                if (LOG_TO_FILE)
                    log_printf("bt: connected to \"%s\"", bt_conn_name);
                set_state(CONNECTED, now);
            }
        } else if (bt_state == CONNECTED) {
            bt_paired = 0;
            set_state(READY, now);
        }
        break;
    case 'L':
    case 'H':
        if (size == 4 && b[2] == 'E' && b[3] == 'R') {
            bt_failed = 1;
            if (bt_state == CONNECTING)
                set_state(READY, now);
        } else if (b[1] == 'H') {
            bt_paired = 1;
            if (bt_state != CONNECTED) {
                copy_name(bt_conn_name, (const u8 *)bt_name[bt_target], NAME_MAX);
                set_state(CONNECTED, now);
            }
        } else if (b[2] == 'O' && b[3] == 'K') {
            bt_paired = 1;
            if (bt_state == CONNECTING)
                set_state(READY, now);
        }
        break;
    case 'T':
        bt_paired = 0;
        if (bt_state == CONNECTED || bt_state == CONNECTING)
            set_state(READY, now);
        break;
    case 'P':                                     /* the headphones report paused / playing */
    case 'C':
        bt_play_state = b[1] == 'C' ? 2u : 1u;
        bt_quiet_until = now + 50000u;            /* as Podium: no status queries for a while */
        bt_remote = b[1] == 'C' ? 4u : 3u;
        break;
    case 'A':
        bt_remote = 1;
        break;
    case 'B':
        bt_remote = 2;
        break;
    default:
        break;
    }
}

static void drop_front(u32 k)
{
    for (u32 i = k; i < bt_rx_len; i++)
        bt_rx[i - k] = bt_rx[i];
    bt_rx_len -= k;
}

static void receive(u32 now)
{
    for (u32 n = 0; n < 128u && (UART_LSR & 1u); n++) {
        u8 c = (u8)UART_DATA;
        bt_last_rx = now;
        if (bt_rx_len == 0 && c != 'C')
            continue;                             /* resync on 'C' (also drops the NULs) */
        if (bt_rx_len < RX_MAX)
            bt_rx[bt_rx_len++] = c;
        for (;;) {
            u32 size = frame_size();
            if (!size || size > bt_rx_len)
                break;
            handle_frame(bt_rx, size, now);
            drop_front(size);
            while (bt_rx_len && bt_rx[0] != 'C')
                drop_front(1);
        }
    }
    if (bt_rx_len && now - bt_last_rx > 200u)    /* cut-off frame */
        bt_rx_len = 0;
}

/* ---- actions ------------------------------------------------------------------ */

void bt_power(u32 on)
{
    u32 now = ms_now();
    pins_off();
    bt_rx_len = 0;
    bt_paired = 0;
    bt_failed = 0;
    if (on) {
        uart_open();
        gpio_set(2, 7, 1);
        bt_version = 0;
        set_state(STARTING, now);
        if (LOG_TO_FILE)
            log_printf("bt: power on");
    } else {
        set_state(OFF, now);
    }
}

/* charging screen / power off: module off, no logging, no drawing */
#ifdef ECHO_PORT
extern void gpio_set_orig(u32 bank, u32 pin, u32 level);
extern volatile u32 echo_mute_level, echo_wired_level;
#endif
extern volatile u32 bt_routed;

void bt_shutdown(void)
{
#ifdef ECHO_PORT
    if (bt_routed) {                              /* jacks get their pins back, quietly */
        bt_routed = 0;
        gpio_set_orig(2, 1, echo_mute_level);
        gpio_set_orig(2, 14, echo_wired_level);
    }
#endif
    if (bt_state != OFF) {
        pins_off();
        bt_state = OFF;
    }
}

static void start_scan(u32 now)
{
    bt_ndev = 0;
    bt_failed = 0;
    command('S');
    set_state(SCANNING, now);
}

static void stop_scan(u32 now)
{
    command('P');
    set_state(READY, now);
}

static void connect(u32 dev, u32 now)
{
    bt_target = dev;
    bt_failed = 0;
    address_command('L', dev);
    bt_answered = 0;
    bt_repeated = 0;
    if (LOG_TO_FILE)
        log_printf("bt: connecting to \"%s\"", bt_name[dev]);
    set_state(CONNECTING, now);
}

static void disconnect(u32 now)
{
    command('T');
    bt_paired = 0;
    set_state(READY, now);
}

/* ---- streaming (Podium's rules: platform/device/logic/bt_module.c, app/bluetooth_link.c) ---- */

#define player_state  FN(0x03057BD0, int (*)(void))
#define jacks         FN(0x03055EC0, u32 (*)(void))
#define player_pause  FN(0x03057C94, void (*)(int pause))   /* RECHO's Play key: 1 pause, 0 play */
#define player_skip   FN(0x0305AB80, void (*)(int dir))     /* RECHO's hold keys: +1 next, -1 prev */
#define ECHO_MS       800u                                  /* ignore the headphones' echo of our change */

static void stream_frame(u32 on)
{
    u8 f[5] = {'C', 'Y', '0', (u8)(on ? 0x1Eu : 0u), 0};
    send_bytes(f, 5);
}

static void set_stream(u32 on, u32 now)
{
    u8 notice = 0;
    if (on && bt_play_state == 1u) {
        notice = 0x86;                            /* play notice first */
        bt_play_state = 2;
    } else if (!on && bt_play_state == 2u) {
        notice = 0x87;                            /* pause notice first */
        bt_play_state = 1;
    }
    if (notice) {
        u8 f[5] = {'C', notice, '0', '0', 0};
        send_bytes(f, 5);
        bt_pending = 1;
        bt_pending_on = on;
        bt_pending_at = now + 100u;
    } else {
        stream_frame(on);
    }
    if (LOG_TO_FILE)
        log_printf("bt: stream %s (notice %02x)", on ? "on" : "off", notice);
}

extern volatile u32 bt_routed;           /* ui_np.c: Now Playing shows "BT" */

static void follow_player(u32 now)
{
    if (bt_hello) {                               /* first thing after connecting */
        bt_hello = 0;
        stream_frame(0);
        return;
    }
    if (bt_pending && (int)(now - bt_pending_at) >= 0) {
        bt_pending = 0;
        stream_frame(bt_pending_on);
    }
    int playing = player_state() == 1;
#ifdef ECHO_PORT
    /* jack readings are not trusted while routed and for 500 ms after a routing change */
    if (!playing)
        bt_wired = 0;
    else if (!bt_routed && now - bt_changed_at > 500u && jacks())
        bt_wired = 1;
    u32 stream = playing && !bt_wired;
#else
    u32 stream = 0;                               /* Mini: no routing yet */
#endif
    if (stream != bt_streamed) {
        bt_streamed = stream;
        bt_changed_at = now;
        set_stream(stream, now);
#ifdef ECHO_PORT
        bt_route(stream);
#endif
    }
    u32 r = bt_remote;
    if (r) {
        bt_remote = 0;
        int settled = now - bt_changed_at >= ECHO_MS;
        if (r == 1u)
            player_skip(-1);
        else if (r == 2u)
            player_skip(1);
        else if (r == 3u && settled && bt_streamed && playing) {
            player_pause(1);
            bt_buds_paused = 1;
        } else if (r == 4u && settled && bt_buds_paused && player_state() == 2) {
            player_pause(0);
            bt_buds_paused = 0;
        }
    }
    if (player_state() != 2)
        bt_buds_paused = 0;
}

#ifdef ECHO_PORT
/* Route the music to the module (on) or back to the jacks. Podium's route_to_module() and
 * match_stock_housekeeping() (board/bluetooth.c), Echo pins from Podium's board_pins.c. */
extern void gpio_set_orig(u32 bank, u32 pin, u32 level);
extern volatile u32 echo_mute_level, echo_wired_level;   /* echo_port.c: what RECHO last asked for */
STATE volatile u32 bt_stock_done = 0;
#define gpio_read_pin FN(0x03051610, u32 (*)(u32 bank, u32 pin))
/* Test build: each time streaming starts, the next routing variant is tried and logged.
 *   0  DAC mute GPIO2.1 high, wired output GPIO2.14 low   (Podium's route_to_module)
 *   1  DAC mute high, GPIO2.14 left as RECHO set it        (DAC A stays enabled)
 *   2  as 1, and GPIO2.9 low                               (Podium's notes)
 *   3  as 0, and GPIO2.9 low */
STATE volatile u32 bt_variant = 0;
STATE volatile u32 bt_variant_used = 0;

static void log_audio_state(const char *when)
{
    if (!LOG_TO_FILE)
        return;
    log_printf("bt: %s variant %u: I2S1 TXCR %08x RXCR %08x CKR %08x XFER %08x DMACR %08x FIFOLR %08x",
               when, bt_variant_used, REG(0x40030000), REG(0x40030004), REG(0x40030008), REG(0x4003001C),
               REG(0x40030010), REG(0x4003000C));
    log_printf("bt: %s pins 0.14=%u 2.0=%u 2.1=%u 2.2=%u 2.9=%u 2.14=%u 1.4=%u GRF+BC %08x +C4 %08x",
               when, gpio_read_pin(0, 14), gpio_read_pin(2, 0), gpio_read_pin(2, 1), gpio_read_pin(2, 2),
               gpio_read_pin(2, 9), gpio_read_pin(2, 14), gpio_read_pin(1, 4), REG(0x500100BC), REG(0x500100C4));
}

void bt_route(u32 on)
{
    if ((on != 0u) == (bt_routed != 0u))
        return;
    if (on && !bt_stock_done) {
        bt_stock_done = 1;
        if (LOG_TO_FILE)
            log_printf("bt: before stock settings GRF+C4 %08x CRU+84 %08x +A0 %08x +C0 %08x",
                       REG(0x500100C4), REG(0x40180084), REG(0x401800A0), REG(0x401800C0));
        REG(0x500100C4) = 0x00010000u;            /* GRF +0xC4 bit 0 cleared */
        REG(0x40180084) = 0x00920092u;            /* gates 0x11, 0x14, 0x17 off */
        REG(0x401800A0) = 0x00020002u;            /* gate 0x81 off */
        REG(0x401800C0) = 0x00680068u;            /* I2S0 and SPI0/1 held in reset */
        REG(0x50010014) = 0x00200020u;            /* jack sense pulls as stock */
        REG(0x50010054) = 0x00010001u;
    }
    if (on) {
        bt_variant_used = bt_variant;
        bt_variant = (bt_variant + 1u) & 3u;
        log_audio_state("before");
    }
    u32 v = bt_variant_used;
    bt_routed = on ? (v == 1u || v == 2u ? 2u : 1u) : 0u;  /* 2 = leave GPIO2.14 alone */
    bt_changed_at = ms_now();
    gpio_set_orig(2, 1, on ? 1u : echo_mute_level);       /* DAC mute */
    if (!on || v == 0u || v == 3u)
        gpio_set_orig(2, 14, on ? 0u : echo_wired_level); /* wired output */
    if (on && (v == 2u || v == 3u))
        gpio_set_orig(2, 9, 0);
    if (LOG_TO_FILE)
        log_printf("bt: route %s (variant %u)", on ? "to the headphones" : "to the jacks", v);
    if (on)
        log_audio_state("after");
}
#endif

/* ---- tick (POWER service, every main-loop pass) --------------------------------- */

extern const u32 bt_screen[];

void bt_tick(void)
{
    if (bt_open_pending) {
        bt_open_pending = 0;
        bt_list[1] = 0;
        bt_top = 0;
        close_module();                           /* as the cable chooser opens */
        switch_screen(bt_screen);
    }
    if (bt_state == OFF)
        return;
    u32 now = ms_now();
    receive(now);
    u32 age = now - bt_since;
    switch (bt_state) {
    case STARTING:
        if (age >= READY_WAIT_MS) {
            pins_off();
            set_state(NO_REPLY, now);
        }
        return;
    case NO_REPLY:
        return;
    case SCANNING:
        if (age >= SCAN_MS) {
            stop_scan(now);
            return;
        }
        if (!bt_answered && !bt_repeated && age >= RETRY_MS) {
            bt_repeated = 1;
            u8 f[5] = {'C', 'S', '0', '0', 0};
            send_bytes(f, 5);
        }
        break;
    case CONNECTING:
        if (age >= CONNECT_MS) {
            bt_failed = 1;
            command('T');
            set_state(READY, now);
            return;
        }
        if (!bt_answered && !bt_repeated && age >= RETRY_MS) {
            bt_repeated = 1;
            address_command('H', bt_target);
        }
        break;
    default:
        break;
    }
    if (bt_state == CONNECTED)
        follow_player(now);
    u32 period = bt_state == CONNECTING ? QUERY_FAST_MS : QUERY_MS;
    if (now - bt_last_query >= period && (int)(now - bt_quiet_until) >= 0) {
        bt_last_query = now;
        command('Q');
    }
}

/* ---- Settings row ---------------------------------------------------------------- */

void bt_request_open(void)
{
    bt_open_pending = 1;
}

u32 bt_powered(void)
{
    return bt_state != OFF;
}

/* m48 value draw: returns the On/Off flag in r1, keeps every other register */
__attribute__((naked)) void bt_value_r1(void)
{
    __asm__ volatile(
        "push   {r0, r2, r3, r4, ip, lr}\n"
        "bl     bt_powered\n"
        "mov    r1, r0\n"
        "pop    {r0, r2, r3, r4, ip, pc}\n");
}

const char str_bluetooth[] = "Bluetooth";

/* ---- screen ---------------------------------------------------------------------- */

static int font_h(int font)
{
    int h = FONTS[font * 4 + 2];
    return (h > 0 && h < 40) ? h : 12;
}

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

static void text_right(int font, int x_end, int y_mid, const char *s, int maxw, u32 fg)
{
    if (!s || !*s)
        return;
    int w = text_width(font, s);
    if (w > maxw)
        w = maxw;
    text_at(font, x_end - w, y_mid, s, maxw, fg);
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

static u32 row_count(void)
{
    u32 s = bt_state;
    if (s == OFF || s == STARTING || s == NO_REPLY)
        return 1;
    return 2 + bt_ndev;
}

static u32 scan_left_s(void)
{
    u32 age = ms_now() - bt_since;
    return age >= SCAN_MS ? 0 : (SCAN_MS - age + 999u) / 1000u;
}

static const char *status_text(char *buf, u32 *colour)
{
    *colour = SOFT;
    switch (bt_state) {
    case STARTING:
        return "starting";
    case NO_REPLY:
        *colour = RED;
        return "no reply";
    case SCANNING: {
        char *p = put_str(buf, "searching ", buf + 23);
        p = put_num(p, scan_left_s(), buf + 23);
        p = put_str(p, " s", buf + 23);
        *p = 0;
        *colour = AMBER;
        return buf;
    }
    case CONNECTING:
        *colour = AMBER;
        return "connecting";
    case CONNECTED:
        *colour = GREEN;
        return "connected";
    case READY:
        return "on";
    default:
        return "off";
    }
}

static void row(u32 i, int y, const char *label, const char *value, u32 vcolour)
{
    u32 sel = bt_list[1] == i;
    if (sel)
        fill_rect(0, y, W - 1, y + ROW_H - 1, SELBG);
    int vw = value && *value ? text_width(0, value) + 12 : 0;
    text_at(1, 10, y + ROW_H / 2, label, W - 20 - vw, sel ? WHITE : SOFT);
    if (vw)
        text_right(0, W - 10, y + ROW_H / 2, value, 140, vcolour);
}

void bt_draw(void)
{
    fill_rect(0, 0, W - 1, 169, BLACK);
    char sbuf[24];
    u32 sc;
    const char *st = status_text(sbuf, &sc);
    text_at(1, 8, 10, "Bluetooth", 150, WHITE);
    text_right(1, W - 8, 10, st, 150, sc);
    fill_rect(0, HEADER_H - 1, W - 1, HEADER_H - 1, RULE);

    u32 n = row_count();
    bt_list[0] = n;
    if (bt_list[1] >= n)
        bt_list[1] = n - 1;
    if (bt_list[1] < bt_top)
        bt_top = bt_list[1];
    if (bt_list[1] >= bt_top + ROWS)
        bt_top = bt_list[1] - ROWS + 1;

    for (u32 k = 0; k < ROWS && bt_top + k < n; k++) {
        u32 i = bt_top + k;
        int y = HEADER_H + (int)k * ROW_H;
        if (i == 0) {
            row(i, y, "Bluetooth", bt_state == OFF ? "Off" : "On", bt_state == OFF ? GREY : GREEN);
        } else if (i == 1) {
            if (bt_state == CONNECTED)
                row(i, y, "Disconnect", bt_conn_name, SOFT);
            else if (bt_state == SCANNING)
                row(i, y, "Stop searching", 0, 0);
            else
                row(i, y, "Search for headphones", 0, 0);
        } else {
            u32 d = i - 2;
            const char *v = 0;
            u32 vc = SOFT;
            if (bt_state == CONNECTING && d == bt_target) {
                v = "connecting";
                vc = AMBER;
            } else if (bt_state == CONNECTED && d == bt_target) {
                v = "connected";
                vc = GREEN;
            } else if (bt_failed && d == bt_target) {
                v = "failed";
                vc = RED;
            }
            row(i, y, bt_name[d], v, vc);
        }
    }

    const char *hint = 0;
    switch (bt_state) {
    case OFF:
        hint = "Turn on, then search for your headphones";
        break;
    case NO_REPLY:
        hint = "The Bluetooth module did not answer";
        break;
    case SCANNING:
        hint = "Put your headphones in pairing mode";
        break;
    case READY:
        hint = bt_ndev ? "Select headphones to connect" : "Put headphones in pairing mode, then search";
        break;
    case CONNECTING:
        hint = "Connecting - keep the headphones close";
        break;
    case CONNECTED:
        hint = "Sound over Bluetooth arrives in a later update";
        break;
    default:
        break;
    }
    fill_rect(0, 145, W - 1, 145, RULE);
    if (hint) {
        int w = text_width(0, hint);
        text_at(0, (W - (w > W - 16 ? W - 16 : w)) / 2, 158, hint, W - 16, DIM);
    }
    bt_drawn_s = bt_state == SCANNING ? scan_left_s() : 0;
    bt_dirty = 0;
}

static void select_row(u32 now)
{
    u32 i = bt_list[1];
    u32 s = bt_state;
    if (i == 0) {
        bt_power(s == OFF || s == NO_REPLY);
    } else if (i == 1) {
        if (s == CONNECTED)
            disconnect(now);
        else if (s == SCANNING)
            stop_scan(now);
        else if (s == READY)
            start_scan(now);
    } else if (i - 2 < bt_ndev && (s == READY || s == SCANNING)) {
        connect(i - 2, now);
    }
    bt_dirty = 1;
}

int bt_key(const u8 *key)
{
    bt_list[0] = row_count();
    if (list_keys(bt_list, key))
        return 1;
    if (key[1] == 2) {                            /* tap */
        if (key[0] == 4)
            select_row(ms_now());
        else if (key[0] == 5 || key[0] == 6)
            go_main_menu();                       /* back to the main menu */
    }
    return 1;
}

int bt_screen_tick(void)
{
    if (bt_dirty || (bt_state == SCANNING && scan_left_s() != bt_drawn_s)) {
        bt_dirty = 0;
        request_redraw(current_screen());
    }
    return 0;
}

void bt_enter(void)
{
    bt_dirty = 1;
}

/* RECHO screen descriptor, laid out as the cable chooser's (USBSEL at 0x3060AB0) */
const u32 bt_screen[14] = {
    0x45535442u, 0x00000052u,                     /* "BTSCR" */
    0, 0, 0, 0,                                   /* rect: whole screen, as USBSEL */
    (u32)bt_enter, (u32)bt_key, (u32)bt_screen_tick, (u32)bt_draw,
    0,                                            /* +40 leave */
    0,                                            /* +44 soft keys: none */
    0, 0,
};
