/*
 * Bluetooth diagnostic (both players) — step 1 of Bluetooth support.
 *
 * Runs once per boot, 10 s after start-up, only while Settings -> Log to file is on
 * (settings byte 0x0303EDD5). It powers the Bluetooth module for 25 s, listens on its
 * serial line and writes everything to ECHOLOG.TXT through RECHO's own log (0x306C9EC).
 * No audio routing, no pairing, nothing written anywhere else.
 *
 * Hardware facts from Podium (full-size Echo, board/bluetooth.c), assumed identical on the
 * Echo Mini until this log says otherwise:
 *   module power    GPIO2.7 (high = on)
 *   serial          UART1 at 0x400B0000, pins GPIO2.16 / GPIO2.17 (function 1), group A
 *                   (GRF 0x50010000 + 0xBC = 0x02000000), 115,200 8N1 from the 24 MHz crystal
 *                   (CRU + 0x38: 0x00C00040 then 0x003F0000; divisor 13)
 *   clocks / reset  CRU 0x40180000: reset id 8 (+0xC0 bit 8), gate id 29 (+0x84 bit 13),
 *                   bus gate id 137 (+0xA0 bit 9); CRU registers are write-masked (high half)
 *   protocol        ASCII frames "C" + letter + 2 chars, NUL-terminated. The module sends
 *                   "CV1x" when ready (x = version); "CQ00" asks for its status.
 * RECHO's own helpers: 0x3051590 gpio_set(bank, pin, level) (makes the pin a GPIO output),
 * 0x3051570 gpio_function(bank, pin, function).
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

#define gpio_set      FN(0x03051590, void (*)(u32 bank, u32 pin, u32 level))
#define gpio_function FN(0x03051570, void (*)(u32 bank, u32 pin, u32 function))
#define ms_now        FN(0x03051638, u32 (*)(void))
#define delay_ms      FN(0x03051C48, void (*)(u32 ms))
#define log_printf    FN(0x0306C9EC, void (*)(const char *fmt, ...))

#define LOG_TO_FILE   (((volatile u8 *)lit(0x0303EDD0u))[5])   /* settings block byte 5: log=on/off */

#define UART_DATA     REG(0x400B0000)                     /* RBR / THR, DLL with DLAB */
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

#define START_AFTER_MS 10000u
#define LISTEN_MS      25000u
#define QUERY_EVERY_MS 1000u
#define LINE_MAX       48

#define STATE __attribute__((section(".text.bt_state,\"ax\",%progbits@"), used))
STATE volatile u32 bt_phase = 0;          /* 0 waiting, 1 listening, 2 done */
STATE volatile u32 bt_t0 = 0;
STATE volatile u32 bt_last_query = 0;
STATE volatile u32 bt_last_rx = 0;
STATE volatile u32 bt_rx_total = 0;
STATE volatile u32 bt_queries = 0;
STATE volatile u32 bt_version = 0;
STATE volatile u32 bt_line_len = 0;
STATE char bt_line[LINE_MAX + 1] = {0};
STATE char bt_hex[LINE_MAX * 3 + 1] = {0};

static const char HEXD[] = "0123456789abcdef";

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
    CRU_RESET_0 = 0x01000000u;            /* release */
    CRU_GATE_1 = 0x20000000u;             /* UART1 clock on */
    CRU_GATE_8 = 0x02000000u;             /* UART1 bus clock on */
    CRU_UART_CLK = 0x00C00040u;           /* from the crystal */
    CRU_UART_CLK = 0x003F0000u;           /* divider 1 */
    UART_SRR = 7;                         /* reset UART, RX and TX FIFOs */
    UART_IER_DLH = 0;                     /* no interrupts: polled */
    UART_DMASA = 1;
    UART_MCR = 0;
    UART_SFE = 1;                         /* FIFOs on */
    UART_SRT = 0;
    UART_STET = 0;
    UART_LCR = 3;                         /* 8N1 */
    UART_LCR = 3 | 0x80;                  /* divisor latch */
    UART_DATA = 13;                       /* 115,200 */
    UART_IER_DLH = 0;
    UART_LCR = 3;
    UART_SRR = 6;                         /* clear FIFOs */
    gpio_function(2, 16, 1);
    gpio_function(2, 17, 1);
}

static void send_frame(const char *s)
{
    for (u32 i = 0;; i++) {
        for (u32 polls = 0; (UART_USR & 2u) == 0 && polls < 100000u; polls++)
            ;
        UART_DATA = (u8)s[i];
        if (!s[i])
            break;                        /* frames end with their NUL */
    }
}

static void flush_line(void)
{
    if (!bt_line_len)
        return;
    bt_line[bt_line_len] = 0;
    char *h = bt_hex;
    for (u32 i = 0; i < bt_line_len; i++) {
        u8 c = (u8)bt_line[i];
        *h++ = HEXD[c >> 4];
        *h++ = HEXD[c & 15];
        *h++ = ' ';
        if (c < 0x20 || c > 0x7E)
            bt_line[i] = '.';
    }
    *h = 0;
    log_printf("bt diag: rx \"%s\" [%s]", bt_line, bt_hex);
    if (bt_line_len >= 4 && bt_line[0] == 'C' && bt_line[1] == 'V')
        bt_version = (u32)(u8)bt_line[3];
    bt_line_len = 0;
}

void bt_diag_tick(void)
{
    u32 now = ms_now();
    if (bt_phase == 0) {
        if (now < START_AFTER_MS || !LOG_TO_FILE)
            return;
        log_printf("bt diag: power on GPIO2.7, UART1 115200 on GPIO2.16/2.17, listening %u s", LISTEN_MS / 1000u);
        pins_off();
        uart_open();
        gpio_set(2, 7, 1);
        bt_t0 = bt_last_query = bt_last_rx = now;
        bt_phase = 1;
        return;
    }
    if (bt_phase != 1)
        return;
    for (u32 n = 0; n < 64u && (UART_LSR & 1u); n++) {
        u8 c = (u8)UART_DATA;
        bt_rx_total++;
        bt_last_rx = now;
        if (c == 0) {
            flush_line();
            continue;
        }
        bt_line[bt_line_len++] = (char)c;
        if (bt_line_len >= LINE_MAX)
            flush_line();
    }
    if (bt_line_len && now - bt_last_rx > 50u)
        flush_line();
    if (now - bt_t0 > 2000u && now - bt_last_query >= QUERY_EVERY_MS) {
        send_frame("CQ00");
        bt_queries++;
        bt_last_query = now;
    }
    if (now - bt_t0 >= LISTEN_MS) {
        flush_line();
        pins_off();
        log_printf("bt diag: done, %u bytes from the module, %u status queries, version char %u (0 = none)",
                   bt_rx_total, bt_queries, bt_version);
        bt_phase = 2;
    }
}
