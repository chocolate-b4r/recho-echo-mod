/*
 * Bluetooth diagnostic (both players) - step 2 of Bluetooth support: scan and connect.
 *
 * Runs once per boot, 10 s after start-up, only while Settings -> Log to file is on
 * (settings block 0x0303EDD0, byte 5). Sequence, all logged through RECHO's own log:
 *   1. power the module, open UART1, wait for its ready report "CV1x" (5 s at most)
 *   2. scan for 30 s ("CS00", as long as Podium scans); every device found is logged with its address and name
 *   3. connect to the first device found ("CL06" + address, "CH06" + address 4 s later),
 *      query the status every second ("CQ00"); "CQO.." means connected
 *   4. 5 s after connecting (or 30 s without), disconnect ("CT00") and power the module off
 * No audio is routed; nothing is written anywhere except the log. Put the headphones in
 * pairing mode before the run.
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
#define READY_WAIT_MS  5000u
#define SCAN_MS        30000u
#define CONNECT_MS     30000u
#define CONNECTED_HOLD 5000u
#define QUERY_EVERY_MS 1000u
#define RX_MAX         64

#define STATE __attribute__((section(".text.bt_state,\"ax\",%progbits@"), used))
STATE volatile u32 bt_phase = 0;          /* 0 wait, 1 ready?, 2 scan, 3 connect, 4 hold, 5 done */
STATE volatile u32 bt_t = 0;              /* start of the current phase */
STATE volatile u32 bt_last_query = 0;
STATE volatile u32 bt_last_rx = 0;
STATE volatile u32 bt_rx_total = 0;
STATE volatile u32 bt_version = 0;
STATE volatile u32 bt_found = 0;
STATE volatile u32 bt_connected = 0;
STATE volatile u32 bt_ch_sent = 0;
STATE volatile u32 bt_rx_len = 0;
STATE u8 bt_rx[RX_MAX] = {0};
STATE u8 bt_target[6] = {0};
STATE volatile u32 bt_have_target = 0;
STATE char bt_text[RX_MAX + 1] = {0};
STATE char bt_hex[RX_MAX * 3 + 1] = {0};

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
}

static void address_command(char letter)          /* "C" letter "06" + 6-byte address */
{
    u8 f[10];
    f[0] = 'C';
    f[1] = (u8)letter;
    f[2] = '0';
    f[3] = '6';
    for (u32 i = 0; i < 6; i++)
        f[4 + i] = bt_target[i];
    send_bytes(f, 10);
}

static void hex_of(const u8 *b, u32 n)
{
    char *h = bt_hex;
    for (u32 i = 0; i < n; i++) {
        *h++ = HEXD[b[i] >> 4];
        *h++ = HEXD[b[i] & 15];
        *h++ = ' ';
    }
    *h = 0;
}

static void text_of(const u8 *b, u32 n)
{
    for (u32 i = 0; i < n; i++)
        bt_text[i] = (b[i] >= 0x20 && b[i] <= 0x7E) ? (char)b[i] : '.';
    bt_text[n] = 0;
}

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
    case 'S':                                     /* "CS" nn + address + name, or "CSEN" */
        if (n < 4)
            return 0;
        if (!digit(b[2]) || !digit(b[3]))
            return 4;
        need = 4u + (u32)(b[2] - '0') * 10u + (u32)(b[3] - '0');
        if (need >= 4u + 38u)
            return 4;
        return n >= need ? need : 0;
    case 'Q':                                     /* "CQ" + status; "CQO" nn + address + name */
        if (n < 3)
            return 0;
        if (b[2] != 'O')
            return 3;
        if (n < 5)
            return 0;
        if (!digit(b[3]) || !digit(b[4]))
            return 5;
        need = 5u + (u32)(b[3] - '0') * 10u + (u32)(b[4] - '0');
        return n >= need ? need : 0;
    case 'T':
    case 'H':                                     /* "CHOK" / "CHER", else 2 bytes */
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

static void handle_frame(const u8 *b, u32 size)
{
    hex_of(b, size);
    text_of(b, size);
    if (b[1] == 'S' && size > 4 + 6) {           /* a device from the scan */
        bt_found++;
        log_printf("bt diag: found %02x:%02x:%02x:%02x:%02x:%02x \"%s\"",
                   b[4], b[5], b[6], b[7], b[8], b[9], bt_text + 10);
        if (!bt_have_target) {
            for (u32 i = 0; i < 6; i++)
                bt_target[i] = b[4 + i];
            bt_have_target = 1;
        }
        return;
    }
    log_printf("bt diag: rx \"%s\" [%s]", bt_text, bt_hex);
    if (size >= 4 && b[1] == 'V')
        bt_version = b[3];
    if (size >= 3 && b[1] == 'Q' && b[2] == 'O' && !bt_connected) {
        bt_connected = 1;
        log_printf("bt diag: CONNECTED to \"%s\"", size > 11 ? bt_text + 11 : "?");
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
    for (u32 n = 0; n < 64u && (UART_LSR & 1u); n++) {
        u8 c = (u8)UART_DATA;
        bt_rx_total++;
        bt_last_rx = now;
        if (bt_rx_len == 0 && c != 'C')
            continue;                             /* resync on 'C' (also drops the NULs) */
        if (bt_rx_len < RX_MAX)
            bt_rx[bt_rx_len++] = c;
        for (;;) {
            u32 size = frame_size();
            if (!size || size > bt_rx_len)
                break;
            handle_frame(bt_rx, size);
            drop_front(size);
            while (bt_rx_len && bt_rx[0] != 'C')
                drop_front(1);
        }
    }
    if (bt_rx_len && now - bt_last_rx > 200u) {  /* cut-off frame: log and drop */
        hex_of(bt_rx, bt_rx_len);
        log_printf("bt diag: partial [%s]", bt_hex);
        bt_rx_len = 0;
    }
}

static void next_phase(u32 phase, u32 now)
{
    bt_phase = phase;
    bt_t = now;
}

void bt_diag_tick(void)
{
    u32 now = ms_now();
    if (bt_phase == 0) {
        if (now < START_AFTER_MS || !LOG_TO_FILE)
            return;
        log_printf("bt diag: step 2 - power on GPIO2.7, UART1 115200 on GPIO2.16/2.17");
        pins_off();
        uart_open();
        gpio_set(2, 7, 1);
        bt_last_query = now;
        next_phase(1, now);
        return;
    }
    if (bt_phase >= 5)
        return;
    receive(now);

    if (bt_phase == 1 && (bt_version || now - bt_t >= READY_WAIT_MS)) {
        log_printf("bt diag: scanning %u s (module version char %u)", SCAN_MS / 1000u, bt_version);
        command('S');
        next_phase(2, now);
    } else if (bt_phase == 2 && now - bt_t >= SCAN_MS) {
        command('P');                             /* stop scanning */
        log_printf("bt diag: scan done, %u device(s)", bt_found);
        if (bt_have_target) {
            log_printf("bt diag: connecting to %02x:%02x:%02x:%02x:%02x:%02x", bt_target[0], bt_target[1],
                       bt_target[2], bt_target[3], bt_target[4], bt_target[5]);
            address_command('L');
            bt_ch_sent = 0;
            next_phase(3, now);
        } else {
            next_phase(4, now);
        }
    } else if (bt_phase == 3) {
        if (!bt_ch_sent && now - bt_t >= 4000u) {
            address_command('H');
            bt_ch_sent = 1;
        }
        if (now - bt_last_query >= QUERY_EVERY_MS) {
            command('Q');
            bt_last_query = now;
        }
        if (bt_connected || now - bt_t >= CONNECT_MS) {
            if (!bt_connected)
                log_printf("bt diag: no connection after %u s", CONNECT_MS / 1000u);
            next_phase(4, now);
        }
    } else if (bt_phase == 4 && now - bt_t >= (bt_connected ? CONNECTED_HOLD : 0u)) {
        if (bt_connected)
            command('T');                         /* disconnect */
        delay_ms(200);
        receive(ms_now());
        pins_off();
        log_printf("bt diag: done - %u bytes, version char %u, %u device(s), connected %u",
                   bt_rx_total, bt_version, bt_found, bt_connected);
        next_phase(5, now);
    }
}
