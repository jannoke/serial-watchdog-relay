/*
 * oled_display.c – SSD1306 128×32 OLED display driver
 *
 * Renders two lines of status information:
 *   Line 1 (top):    remaining watchdog time / total timeout
 *   Line 2 (bottom): attempts/max · WiFi icon · WiFi-connected icon · heart icon
 *
 * Uses the ESP-IDF new I2C master driver (ESP-IDF ≥ 5.1).
 */

#include "oled_display.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "watchdog_timer.h"
#include "config.h"

static const char *TAG = "oled";

/* ── Timing constants ──────────────────────────────────────────────────────── */

#define HEARTBEAT_SHOW_SECS   60   /* show heart icon if HB received within 60 s */
#define HEARTBEAT_BLINK_MS    1000 /* keep heart ON for 1 s after each new HB    */

/* ── SSD1306 geometry ──────────────────────────────────────────────────────── */

#define OLED_WIDTH   128
#define OLED_HEIGHT  32
#define OLED_PAGES   (OLED_HEIGHT / 8)            /* 4                          */
#define FB_SIZE      (OLED_WIDTH * OLED_PAGES)    /* 512 bytes                  */

/* ── SSD1306 I2C control bytes ─────────────────────────────────────────────── */

#define SSD1306_CTRL_CMD   0x00
#define SSD1306_CTRL_DATA  0x40

/* ── SSD1306 command bytes ─────────────────────────────────────────────────── */

#define SSD1306_DISPLAY_OFF        0xAE
#define SSD1306_DISPLAY_ON         0xAF
#define SSD1306_SET_DISP_CLK       0xD5
#define SSD1306_SET_MUX_RATIO      0xA8
#define SSD1306_SET_DISP_OFFSET    0xD3
#define SSD1306_SET_START_LINE     0x40
#define SSD1306_CHARGE_PUMP        0x8D
#define SSD1306_MEM_ADDR_MODE      0x20
#define SSD1306_SEG_REMAP          0xA1
#define SSD1306_COM_SCAN_DIR       0xC8
#define SSD1306_SET_COM_PINS       0xDA
#define SSD1306_SET_CONTRAST       0x81
#define SSD1306_SET_PRECHARGE      0xD9
#define SSD1306_SET_VCOMH          0xDB
#define SSD1306_DISP_ALL_ON_RESUME 0xA4
#define SSD1306_NORMAL_DISPLAY     0xA6
#define SSD1306_SET_COL_ADDR       0x21
#define SSD1306_SET_PAGE_ADDR      0x22

/* ── Module state ──────────────────────────────────────────────────────────── */

static i2c_master_bus_handle_t  s_bus_handle  = NULL;
static i2c_master_dev_handle_t  s_dev_handle  = NULL;
static bool                     s_initialized = false;

static bool    s_wifi_connected       = false;
static int64_t s_last_heartbeat_ms    = 0;  /* ms since boot, 0 = never        */
static int64_t s_heartbeat_blink_until_ms = 0; /* blink until this time        */

static SemaphoreHandle_t s_mutex = NULL;

/* Framebuffer: [page][column] laid out as a flat 512-byte array               */
static uint8_t s_framebuf[FB_SIZE];

/* ── 5-column (5×8) bitmap font, column-major, LSB = top pixel ─────────────── *
 *
 * Covers printable ASCII 0x20–0x7E.  Each entry is 5 bytes; one byte per     *
 * column.  A 1-pixel blank column is added automatically when drawing so that *
 * characters are 6 pixels wide on screen.                                     */

static const uint8_t FONT5X8[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* 0x20  ' '  */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* 0x21  '!'  */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* 0x22  '"'  */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* 0x23  '#'  */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* 0x24  '$'  */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* 0x25  '%'  */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* 0x26  '&'  */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* 0x27  '\'' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* 0x28  '('  */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* 0x29  ')'  */
    {0x14, 0x08, 0x3E, 0x08, 0x14}, /* 0x2A  '*'  */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* 0x2B  '+'  */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* 0x2C  ','  */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* 0x2D  '-'  */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* 0x2E  '.'  */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* 0x2F  '/'  */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0x30  '0'  */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 0x31  '1'  */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 0x32  '2'  */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 0x33  '3'  */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 0x34  '4'  */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 0x35  '5'  */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 0x36  '6'  */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 0x37  '7'  */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 0x38  '8'  */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 0x39  '9'  */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* 0x3A  ':'  */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* 0x3B  ';'  */
    {0x08, 0x14, 0x22, 0x41, 0x00}, /* 0x3C  '<'  */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* 0x3D  '='  */
    {0x00, 0x41, 0x22, 0x14, 0x08}, /* 0x3E  '>'  */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* 0x3F  '?'  */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* 0x40  '@'  */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* 0x41  'A'  */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* 0x42  'B'  */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* 0x43  'C'  */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* 0x44  'D'  */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* 0x45  'E'  */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* 0x46  'F'  */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* 0x47  'G'  */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* 0x48  'H'  */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* 0x49  'I'  */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* 0x4A  'J'  */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* 0x4B  'K'  */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* 0x4C  'L'  */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* 0x4D  'M'  */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* 0x4E  'N'  */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* 0x4F  'O'  */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* 0x50  'P'  */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* 0x51  'Q'  */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* 0x52  'R'  */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* 0x53  'S'  */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* 0x54  'T'  */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* 0x55  'U'  */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* 0x56  'V'  */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* 0x57  'W'  */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* 0x58  'X'  */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* 0x59  'Y'  */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* 0x5A  'Z'  */
    {0x00, 0x7F, 0x41, 0x41, 0x00}, /* 0x5B  '['  */
    {0x02, 0x04, 0x08, 0x10, 0x20}, /* 0x5C  '\\'  */
    {0x00, 0x41, 0x41, 0x7F, 0x00}, /* 0x5D  ']'  */
    {0x04, 0x02, 0x01, 0x02, 0x04}, /* 0x5E  '^'  */
    {0x40, 0x40, 0x40, 0x40, 0x40}, /* 0x5F  '_'  */
    {0x00, 0x01, 0x02, 0x04, 0x00}, /* 0x60  '`'  */
    {0x20, 0x54, 0x54, 0x54, 0x78}, /* 0x61  'a'  */
    {0x7F, 0x48, 0x44, 0x44, 0x38}, /* 0x62  'b'  */
    {0x38, 0x44, 0x44, 0x44, 0x20}, /* 0x63  'c'  */
    {0x38, 0x44, 0x44, 0x48, 0x7F}, /* 0x64  'd'  */
    {0x38, 0x54, 0x54, 0x54, 0x18}, /* 0x65  'e'  */
    {0x08, 0x7E, 0x09, 0x01, 0x02}, /* 0x66  'f'  */
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, /* 0x67  'g'  */
    {0x7F, 0x08, 0x04, 0x04, 0x78}, /* 0x68  'h'  */
    {0x00, 0x44, 0x7D, 0x40, 0x00}, /* 0x69  'i'  */
    {0x20, 0x40, 0x44, 0x3D, 0x00}, /* 0x6A  'j'  */
    {0x7F, 0x10, 0x28, 0x44, 0x00}, /* 0x6B  'k'  */
    {0x00, 0x41, 0x7F, 0x40, 0x00}, /* 0x6C  'l'  */
    {0x7C, 0x04, 0x18, 0x04, 0x78}, /* 0x6D  'm'  */
    {0x7C, 0x08, 0x04, 0x04, 0x78}, /* 0x6E  'n'  */
    {0x38, 0x44, 0x44, 0x44, 0x38}, /* 0x6F  'o'  */
    {0x7C, 0x14, 0x14, 0x14, 0x08}, /* 0x70  'p'  */
    {0x08, 0x14, 0x14, 0x18, 0x7C}, /* 0x71  'q'  */
    {0x7C, 0x08, 0x04, 0x04, 0x08}, /* 0x72  'r'  */
    {0x48, 0x54, 0x54, 0x54, 0x20}, /* 0x73  's'  */
    {0x04, 0x3F, 0x44, 0x40, 0x20}, /* 0x74  't'  */
    {0x3C, 0x40, 0x40, 0x40, 0x7C}, /* 0x75  'u'  */
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, /* 0x76  'v'  */
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, /* 0x77  'w'  */
    {0x44, 0x28, 0x10, 0x28, 0x44}, /* 0x78  'x'  */
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, /* 0x79  'y'  */
    {0x44, 0x64, 0x54, 0x4C, 0x44}, /* 0x7A  'z'  */
    {0x00, 0x08, 0x36, 0x41, 0x00}, /* 0x7B  '{'  */
    {0x00, 0x00, 0x7F, 0x00, 0x00}, /* 0x7C  '|'  */
    {0x00, 0x41, 0x36, 0x08, 0x00}, /* 0x7D  '}'  */
    {0x10, 0x08, 0x08, 0x10, 0x08}, /* 0x7E  '~'  */
};

/* ── 8×8 icon bitmaps, column-major, LSB = top pixel ──────────────────────── */

/* WiFi icon – concentric arcs with a dot at the bottom */
static const uint8_t ICON_WIFI[8] = {
    0x1C, /* col 0: ..###... */
    0x22, /* col 1: .#...#.. */
    0x5D, /* col 2: #.###.#. */
    0x49, /* col 3: #..#..#. */
    0x5D, /* col 4: #.###.#. */
    0x22, /* col 5: .#...#.. */
    0x1C, /* col 6: ..###... */
    0x00, /* col 7: ........ */
};

/* WiFi+c icon – same arcs but rightmost two cols show a small 'c' */
static const uint8_t ICON_WIFI_C[8] = {
    0x1C, /* col 0 */
    0x22, /* col 1 */
    0x5D, /* col 2 */
    0x49, /* col 3 */
    0x3C, /* col 4: top of 'c' */
    0x42, /* col 5: middle of 'c' */
    0x42, /* col 6: bottom of 'c' */
    0x3C, /* col 7: close 'c'    */
};

/* Heart icon */
static const uint8_t ICON_HEART[8] = {
    0x0C, /* .##..... */
    0x12, /* #..#.... */
    0x24, /* ..#.#... */
    0x48, /* ...#.#.. */
    0x48, /* ...#.#.. */
    0x24, /* ..#.#... */
    0x12, /* #..#.... */
    0x0C, /* .##..... */
};

/* ── Low-level SSD1306 helpers ──────────────────────────────────────────────── */

/* Send a single command byte to the SSD1306. */
static esp_err_t ssd1306_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {SSD1306_CTRL_CMD, cmd};
    return i2c_master_transmit(s_dev_handle, buf, sizeof(buf), 50);
}

/* Send a command byte followed by one argument byte. */
static esp_err_t ssd1306_write_cmd2(uint8_t cmd, uint8_t arg)
{
    uint8_t buf[3] = {SSD1306_CTRL_CMD, cmd, arg};
    return i2c_master_transmit(s_dev_handle, buf, sizeof(buf), 50);
}

/* Flush the entire framebuffer to the display */
static void ssd1306_flush(void)
{
    /* Set column address 0..127 */
    uint8_t col_cmd[3] = {SSD1306_CTRL_CMD, SSD1306_SET_COL_ADDR, 0};
    i2c_master_transmit(s_dev_handle, col_cmd, sizeof(col_cmd), 50);
    uint8_t col_end[2] = {SSD1306_CTRL_CMD, OLED_WIDTH - 1};
    i2c_master_transmit(s_dev_handle, col_end, sizeof(col_end), 50);

    /* Set page address 0..3 */
    uint8_t page_cmd[3] = {SSD1306_CTRL_CMD, SSD1306_SET_PAGE_ADDR, 0};
    i2c_master_transmit(s_dev_handle, page_cmd, sizeof(page_cmd), 50);
    uint8_t page_end[2] = {SSD1306_CTRL_CMD, OLED_PAGES - 1};
    i2c_master_transmit(s_dev_handle, page_end, sizeof(page_end), 50);

    /* Send data – prepend the control byte in a single transfer */
    uint8_t tx[1 + FB_SIZE];
    tx[0] = SSD1306_CTRL_DATA;
    memcpy(&tx[1], s_framebuf, FB_SIZE);
    i2c_master_transmit(s_dev_handle, tx, sizeof(tx), 200);
}

/* ── Framebuffer drawing primitives ─────────────────────────────────────────── */

static void fb_clear(void)
{
    memset(s_framebuf, 0, FB_SIZE);
}

/*
 * Draw an 8-pixel-tall glyph stored in column-major format.
 *
 * @param page   Starting page (0-based; each page = 8 vertical pixels).
 * @param col    Starting column (0-127).
 * @param data   Pointer to 'width' column bytes.
 * @param width  Number of columns (bytes) in the glyph.
 */
static void fb_draw_glyph(int page, int col, const uint8_t *data, int width)
{
    for (int c = 0; c < width; c++) {
        int x = col + c;
        if (x < 0 || x >= OLED_WIDTH) continue;
        if (page < 0 || page >= OLED_PAGES) continue;
        s_framebuf[page * OLED_WIDTH + x] = data[c];
    }
}

/*
 * Draw a printable ASCII character at (page, col) using the 5×8 font.
 * Returns the number of display columns consumed (always 6: 5 + 1 space).
 */
static int fb_draw_char(int page, int col, char ch)
{
    if (ch < 0x20 || ch > 0x7E) ch = ' ';
    int idx = (unsigned char)ch - 0x20;
    fb_draw_glyph(page, col, FONT5X8[idx], 5);
    /* 1-pixel blank spacer (already zero in a cleared buffer) */
    return 6;
}

/*
 * Draw a NUL-terminated string at (page, col).
 * Returns the column after the last drawn character.
 */
static int fb_draw_str(int page, int col, const char *str)
{
    while (*str) {
        col += fb_draw_char(page, col, *str++);
    }
    return col;
}

/*
 * Draw an 8×8 icon at (page, col).  Returns col + 9 (icon + 1-px space).
 */
static int fb_draw_icon(int page, int col, const uint8_t *icon)
{
    fb_draw_glyph(page, col, icon, 8);
    return col + 9;
}

/* ── SSD1306 initialisation ────────────────────────────────────────────────── */

static void ssd1306_init_sequence(void)
{
    ssd1306_write_cmd(SSD1306_DISPLAY_OFF);
    ssd1306_write_cmd2(SSD1306_SET_DISP_CLK,       0x80);
    ssd1306_write_cmd2(SSD1306_SET_MUX_RATIO,      OLED_HEIGHT - 1); /* 0x1F for 32px */
    ssd1306_write_cmd2(SSD1306_SET_DISP_OFFSET,    0x00);
    ssd1306_write_cmd (SSD1306_SET_START_LINE);             /* 0x40 */
    ssd1306_write_cmd2(SSD1306_CHARGE_PUMP,         0x14);  /* enable */
    ssd1306_write_cmd2(SSD1306_MEM_ADDR_MODE,       0x00);  /* horizontal */
    ssd1306_write_cmd (SSD1306_SEG_REMAP);
    ssd1306_write_cmd (SSD1306_COM_SCAN_DIR);
    /* COM pins: sequential, no L/R remap – required for 128×32 */
    ssd1306_write_cmd2(SSD1306_SET_COM_PINS,        0x02);
    ssd1306_write_cmd2(SSD1306_SET_CONTRAST,        0xCF);
    ssd1306_write_cmd2(SSD1306_SET_PRECHARGE,       0xF1);
    ssd1306_write_cmd2(SSD1306_SET_VCOMH,           0x40);
    ssd1306_write_cmd (SSD1306_DISP_ALL_ON_RESUME);
    ssd1306_write_cmd (SSD1306_NORMAL_DISPLAY);
    ssd1306_write_cmd (SSD1306_DISPLAY_ON);
}

/* ── Public API ────────────────────────────────────────────────────────────── */

void oled_display_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t i2c_addr)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    i2c_master_bus_config_t bus_cfg = {
        .clk_source              = I2C_CLK_SRC_DEFAULT,
        .i2c_port                = I2C_NUM_0,
        .scl_io_num              = scl_pin,
        .sda_io_num              = sda_pin,
        .glitch_ignore_cnt       = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = i2c_addr,
        .scl_speed_hz    = 400000,
    };

    err = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return;
    }

    ssd1306_init_sequence();
    fb_clear();
    ssd1306_flush();

    s_initialized = true;
    ESP_LOGI(TAG, "OLED initialized (SDA=%d SCL=%d addr=0x%02X)", sda_pin, scl_pin, i2c_addr);
}

void oled_display_set_wifi_connected(bool connected)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_wifi_connected = connected;
    xSemaphoreGive(s_mutex);
}

void oled_display_heartbeat_received(void)
{
    if (!s_mutex) return;
    int64_t now_ms = esp_timer_get_time() / 1000LL;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_last_heartbeat_ms       = now_ms;
    s_heartbeat_blink_until_ms = now_ms + HEARTBEAT_BLINK_MS;
    xSemaphoreGive(s_mutex);
}

/*
 * Format MM:SS from a total number of milliseconds.
 * Minutes are not capped, so values > 99:59 are represented as "MM:SS".
 */
static void fmt_mmss(char *buf, size_t bufsz, int64_t ms)
{
    if (ms < 0) ms = 0;
    int64_t secs  = ms / 1000;
    int     mins  = (int)(secs / 60);
    int     s     = (int)(secs % 60);
    snprintf(buf, bufsz, "%d:%02d", mins, s);
}

void oled_display_update(void)
{
    if (!s_initialized) return;

    int64_t now_ms = esp_timer_get_time() / 1000LL;

    /* Snapshot shared state */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool    wifi_connected          = s_wifi_connected;
    int64_t last_hb_ms              = s_last_heartbeat_ms;
    int64_t hb_blink_until          = s_heartbeat_blink_until_ms;
    xSemaphoreGive(s_mutex);

    /* Derive display values */
    int64_t remaining_ms  = watchdog_get_remaining_ms();
    int64_t timeout_ms    = (int64_t)watchdog_get_timeout_ms();
    uint8_t attempts      = watchdog_get_restart_attempts();
    uint8_t max_attempts  = watchdog_get_max_attempts();

    /* Heart icon visibility */
    bool hb_recent = (last_hb_ms > 0) &&
                     ((now_ms - last_hb_ms) < (int64_t)(HEARTBEAT_SHOW_SECS * 1000));
    bool hb_blink  = (now_ms < hb_blink_until);
    bool show_heart = hb_recent && hb_blink;  /* blink: on only during blink window */

    /* Build strings */
    char rem_str[12], tot_str[12];
    fmt_mmss(rem_str, sizeof(rem_str), remaining_ms);
    fmt_mmss(tot_str, sizeof(tot_str), timeout_ms);

    char line1[32];
    snprintf(line1, sizeof(line1), "%s / %s", rem_str, tot_str);

    char line2_left[16];
    snprintf(line2_left, sizeof(line2_left), "%d/%d", (int)attempts, (int)max_attempts);

    /* Render */
    fb_clear();

    /* Line 1 – page 0 (top 8 pixels) */
    fb_draw_str(0, 0, line1);

    /* Line 2 – page 2 (pixels 16-23, leaving page 1 as visual gap) */
    int col = fb_draw_str(2, 0, line2_left);
    col += 3; /* small gap */

    /* WiFi icon – always shown (AP is always on once initialized) */
    if (wifi_connected) {
        col = fb_draw_icon(2, col, ICON_WIFI_C);
    } else {
        col = fb_draw_icon(2, col, ICON_WIFI);
    }

    /* Heart icon – right-aligned at column 119 (8 px icon + 1 px margin) */
    if (show_heart) {
        fb_draw_icon(2, 119, ICON_HEART);
    }

    ssd1306_flush();
}
