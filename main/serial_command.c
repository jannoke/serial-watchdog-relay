#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "serial_command.h"
#include "serial_handler.h"
#include "watchdog_timer.h"
#include "relay_controller.h"
#include "nvs_storage.h"
#include "oled_display.h"

#define CMD_BUF_SIZE   256
#define RESP_BUF_SIZE  512

static const char *TAG = "serial_cmd";

static device_config_t *s_config = NULL;
static char   s_line_buf[CMD_BUF_SIZE];
static size_t s_line_len = 0;

/* ── Response helpers ────────────────────────────────────────────────────── */

static void send_str(const char *s)
{
    serial_send((const uint8_t *)s, strlen(s));
}

static void send_ok(void)
{
    send_str("OK\r\n");
}

static void send_ok_value(const char *value)
{
    char buf[RESP_BUF_SIZE];
    snprintf(buf, sizeof(buf), "OK %s\r\n", value);
    send_str(buf);
}

static void send_error(const char *msg)
{
    char buf[RESP_BUF_SIZE];
    snprintf(buf, sizeof(buf), "ERROR %s\r\n", msg);
    send_str(buf);
}

/* ── Command handlers ────────────────────────────────────────────────────── */

static void handle_ping(void)
{
    watchdog_communication_received();
    oled_display_heartbeat_received();
    send_ok();
}

static void handle_status(void)
{
    char buf[RESP_BUF_SIZE];
    int64_t uptime_ms    = esp_timer_get_time() / 1000LL;
    int64_t remaining_ms = watchdog_get_remaining_ms();
    uint8_t attempts     = watchdog_get_restart_attempts();
    bool    relay_state  = relay_is_on();

    snprintf(buf, sizeof(buf),
             "{"
             "\"uptime_ms\":%lld,"
             "\"watchdog_remaining_ms\":%lld,"
             "\"restart_attempts\":%d,"
             "\"max_restart_attempts\":%d,"
             "\"relay_state\":%s,"
             "\"timeout_ms\":%lu,"
             "\"off_period_ms\":%lu"
             "}\r\n",
             (long long)uptime_ms,
             (long long)(remaining_ms > 0 ? remaining_ms : 0),
             (int)attempts,
             (int)s_config->max_restart_attempts,
             relay_state ? "true" : "false",
             (unsigned long)s_config->watchdog_timeout_ms,
             (unsigned long)s_config->turn_off_period_ms);
    send_str(buf);
}

static void handle_get(const char *param)
{
    char val[64];

    if (strcmp(param, "timeout") == 0) {
        snprintf(val, sizeof(val), "%lu",
                 (unsigned long)(s_config->watchdog_timeout_ms / 1000));
    } else if (strcmp(param, "off_period") == 0) {
        snprintf(val, sizeof(val), "%lu",
                 (unsigned long)(s_config->turn_off_period_ms / 1000));
    } else if (strcmp(param, "max_attempts") == 0) {
        snprintf(val, sizeof(val), "%d", (int)s_config->max_restart_attempts);
    } else if (strcmp(param, "relay_pin") == 0) {
        snprintf(val, sizeof(val), "%d", (int)s_config->relay_pin);
    } else if (strcmp(param, "led_pin") == 0) {
        snprintf(val, sizeof(val), "%d", (int)s_config->led_pin);
    } else if (strcmp(param, "button_pin") == 0) {
        snprintf(val, sizeof(val), "%d", (int)s_config->button_pin);
    } else if (strcmp(param, "serial_mode") == 0) {
        snprintf(val, sizeof(val), "%d", (int)s_config->serial_mode);
    } else if (strcmp(param, "wifi_ssid") == 0) {
        snprintf(val, sizeof(val), "%s", s_config->wifi_ssid);
    } else if (strcmp(param, "wifi_password") == 0) {
        snprintf(val, sizeof(val), "%s", s_config->wifi_password);
    } else if (strcmp(param, "wifi_hidden") == 0) {
        snprintf(val, sizeof(val), "%d", (int)s_config->wifi_hidden);
    } else if (strcmp(param, "oled_sda_pin") == 0) {
        snprintf(val, sizeof(val), "%d", (int)s_config->oled_sda_pin);
    } else if (strcmp(param, "oled_scl_pin") == 0) {
        snprintf(val, sizeof(val), "%d", (int)s_config->oled_scl_pin);
    } else if (strcmp(param, "oled_i2c_addr") == 0) {
        snprintf(val, sizeof(val), "0x%02X", (unsigned)s_config->oled_i2c_addr);
    } else if (strcmp(param, "oled_enabled") == 0) {
        snprintf(val, sizeof(val), "%d", (int)s_config->oled_enabled);
    } else {
        send_error("Unknown parameter");
        return;
    }
    send_ok_value(val);
}

static void handle_set(const char *param, const char *value)
{
    if (strcmp(param, "timeout") == 0) {
        unsigned long secs = strtoul(value, NULL, 10);
        if (secs == 0) { send_error("Invalid value"); return; }
        s_config->watchdog_timeout_ms = (uint32_t)(secs * 1000UL);
        watchdog_set_timeout(s_config->watchdog_timeout_ms);
    } else if (strcmp(param, "off_period") == 0) {
        unsigned long secs = strtoul(value, NULL, 10);
        if (secs == 0) { send_error("Invalid value"); return; }
        s_config->turn_off_period_ms = (uint32_t)(secs * 1000UL);
    } else if (strcmp(param, "max_attempts") == 0) {
        s_config->max_restart_attempts = (uint8_t)strtoul(value, NULL, 10);
        watchdog_set_max_attempts(s_config->max_restart_attempts);
    } else if (strcmp(param, "relay_pin") == 0) {
        s_config->relay_pin = (uint8_t)strtoul(value, NULL, 10);
    } else if (strcmp(param, "led_pin") == 0) {
        s_config->led_pin = (uint8_t)strtoul(value, NULL, 10);
    } else if (strcmp(param, "button_pin") == 0) {
        s_config->button_pin = (uint8_t)strtoul(value, NULL, 10);
    } else if (strcmp(param, "serial_mode") == 0) {
        s_config->serial_mode = (serial_mode_t)strtoul(value, NULL, 10);
    } else if (strcmp(param, "wifi_ssid") == 0) {
        strncpy(s_config->wifi_ssid, value, sizeof(s_config->wifi_ssid) - 1);
        s_config->wifi_ssid[sizeof(s_config->wifi_ssid) - 1] = '\0';
    } else if (strcmp(param, "wifi_password") == 0) {
        strncpy(s_config->wifi_password, value, sizeof(s_config->wifi_password) - 1);
        s_config->wifi_password[sizeof(s_config->wifi_password) - 1] = '\0';
    } else if (strcmp(param, "wifi_hidden") == 0) {
        s_config->wifi_hidden = (strtoul(value, NULL, 10) != 0);
    } else if (strcmp(param, "oled_sda_pin") == 0) {
        s_config->oled_sda_pin = (uint8_t)strtoul(value, NULL, 10);
    } else if (strcmp(param, "oled_scl_pin") == 0) {
        s_config->oled_scl_pin = (uint8_t)strtoul(value, NULL, 10);
    } else if (strcmp(param, "oled_i2c_addr") == 0) {
        s_config->oled_i2c_addr = (uint8_t)strtoul(value, NULL, 0);
    } else if (strcmp(param, "oled_enabled") == 0) {
        s_config->oled_enabled = (strtoul(value, NULL, 10) != 0);
    } else {
        send_error("Unknown parameter");
        return;
    }

    esp_err_t err = nvs_storage_save_config(s_config);
    if (err != ESP_OK) {
        send_error("NVS save failed");
        return;
    }
    send_ok();
}

static void handle_reset_timer(void)
{
    watchdog_communication_received();
    oled_display_heartbeat_received();
    send_ok();
}

static void handle_reset_attempts(void)
{
    watchdog_reset_attempts();
    send_ok();
}

static void handle_relay(const char *args)
{
    char          cmd[16] = {0};
    unsigned long ms_ul   = 0;

    int n = sscanf(args, "%15s %lu", cmd, &ms_ul);
    if (n < 1 || cmd[0] == '\0') {
        send_error("Usage: RELAY ON|OFF|CYCLE [ms]");
        return;
    }
    uint32_t ms = (uint32_t)ms_ul;

    /* cmd is already upper-case (caller converted the whole line) */
    if (strcmp(cmd, "ON") == 0) {
        relay_on();
        send_ok();
    } else if (strcmp(cmd, "OFF") == 0) {
        relay_off();
        send_ok();
    } else if (strcmp(cmd, "CYCLE") == 0) {
        if (ms == 0) ms = s_config->turn_off_period_ms;
        relay_cycle(ms);
        send_ok();
    } else {
        send_error("Usage: RELAY ON|OFF|CYCLE [ms]");
    }
}

static void handle_reboot(void)
{
    send_str("OK Rebooting\r\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

static void handle_help(void)
{
    send_str(
        "Commands:\r\n"
        "  PING                    - Keepalive, resets watchdog timer\r\n"
        "  STATUS                  - Get device status as JSON\r\n"
        "  GET <param>             - Get a parameter value\r\n"
        "  SET <param> <value>     - Set a parameter value (persisted to NVS)\r\n"
        "  RESET_TIMER             - Reset watchdog timer\r\n"
        "  RESET_ATTEMPTS          - Reset restart attempt counter\r\n"
        "  RELAY ON|OFF|CYCLE [ms] - Manual relay control\r\n"
        "  REBOOT                  - Reboot the device\r\n"
        "  HELP                    - Show this help\r\n"
        "Parameters: timeout, off_period, max_attempts, relay_pin, led_pin,\r\n"
        "            button_pin, serial_mode, wifi_ssid, wifi_password, wifi_hidden,\r\n"
        "            oled_sda_pin, oled_scl_pin, oled_i2c_addr, oled_enabled\r\n"
        "OK\r\n"
    );
}

/* ── Line processor ──────────────────────────────────────────────────────── */

static void process_line(char *line)
{
    /* Trim trailing whitespace / CR */
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
        line[--len] = '\0';

    if (len == 0) return;

    /* Build an upper-case copy for case-insensitive matching */
    char upper[CMD_BUF_SIZE];
    strncpy(upper, line, sizeof(upper) - 1);
    upper[sizeof(upper) - 1] = '\0';
    for (size_t i = 0; upper[i]; i++)
        upper[i] = (char)toupper((unsigned char)upper[i]);

    ESP_LOGD(TAG, "RX: %s", line);

    if (strcmp(upper, "PING") == 0) {
        handle_ping();

    } else if (strcmp(upper, "STATUS") == 0) {
        handle_status();

    } else if (strncmp(upper, "GET ", 4) == 0) {
        /* Extract parameter name in lower-case from the original line */
        char param[64];
        strncpy(param, line + 4, sizeof(param) - 1);
        param[sizeof(param) - 1] = '\0';
        /* Trim leading spaces */
        char *p = param;
        while (*p == ' ') p++;
        /* Lower-case */
        for (size_t i = 0; p[i]; i++)
            p[i] = (char)tolower((unsigned char)p[i]);
        handle_get(p);

    } else if (strncmp(upper, "SET ", 4) == 0) {
        /* Parse: SET <param> <value>  (use original line for value case) */
        const char *rest = line + 4;
        while (*rest == ' ') rest++;
        const char *space = strchr(rest, ' ');
        if (!space) {
            send_error("Usage: SET <param> <value>");
            return;
        }
        /* param */
        char param[64] = {0};
        size_t plen = (size_t)(space - rest);
        if (plen >= sizeof(param)) plen = sizeof(param) - 1;
        strncpy(param, rest, plen);
        for (size_t i = 0; param[i]; i++)
            param[i] = (char)tolower((unsigned char)param[i]);
        /* value */
        const char *vstart = space + 1;
        while (*vstart == ' ') vstart++;
        char value[128];
        strncpy(value, vstart, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
        handle_set(param, value);

    } else if (strcmp(upper, "RESET_TIMER") == 0) {
        handle_reset_timer();

    } else if (strcmp(upper, "RESET_ATTEMPTS") == 0) {
        handle_reset_attempts();

    } else if (strncmp(upper, "RELAY", 5) == 0) {
        const char *args = upper + 5;
        while (*args == ' ') args++;
        handle_relay(args);

    } else if (strcmp(upper, "REBOOT") == 0) {
        handle_reboot();

    } else if (strcmp(upper, "HELP") == 0) {
        handle_help();

    } else {
        send_error("Unknown command");
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void serial_command_init(device_config_t *config)
{
    s_config   = config;
    s_line_len = 0;
    memset(s_line_buf, 0, sizeof(s_line_buf));
}

void serial_command_process(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];

        if (c == '\n') {
            s_line_buf[s_line_len] = '\0';
            process_line(s_line_buf);
            s_line_len = 0;
        } else if (c == '\r') {
            /* Ignore CR; wait for the following LF */
        } else {
            if (s_line_len < CMD_BUF_SIZE - 1) {
                s_line_buf[s_line_len++] = c;
            } else {
                /* Buffer overflow – discard and report error */
                s_line_len = 0;
                send_error("Command too long");
            }
        }
    }
}
