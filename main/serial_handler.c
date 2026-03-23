#include "serial_handler.h"

#include <string.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* TinyUSB CDC is pulled in via menuconfig / tinyusb component */
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

static const char *TAG = "serial_handler";

static serial_mode_t  s_mode   = SERIAL_MODE_CDC;
static serial_rx_cb_t s_rx_cb  = NULL;

/* ── TTL UART ────────────────────────────────────────────────────────────── */

static void ttl_reader_task(void *arg)
{
    uint8_t buf[TTL_UART_BUF_SIZE];
    while (1) {
        int len = uart_read_bytes(TTL_UART_PORT, buf, sizeof(buf) - 1,
                                  pdMS_TO_TICKS(20));
        if (len > 0 && s_rx_cb) {
            s_rx_cb(buf, (size_t)len);
        }
    }
}

static void init_ttl(void)
{
    uart_config_t cfg = {
        .baud_rate  = TTL_UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(TTL_UART_PORT,
                                        TTL_UART_BUF_SIZE * 2,
                                        TTL_UART_BUF_SIZE * 2,
                                        0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(TTL_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(TTL_UART_PORT,
                                 TTL_UART_TX_PIN, TTL_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreate(ttl_reader_task, "ttl_rx", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "TTL UART initialized (TX=%d RX=%d %d baud)",
             TTL_UART_TX_PIN, TTL_UART_RX_PIN, TTL_UART_BAUD_RATE);
}

/* ── USB CDC ─────────────────────────────────────────────────────────────── */

static void cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    if (event->type != CDC_ACM_DATA_RX_EVENT) {
        return;
    }
    uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
    size_t rx_size = 0;
    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, sizeof(buf), &rx_size);
    if (ret == ESP_OK && rx_size > 0 && s_rx_cb) {
        s_rx_cb(buf, rx_size);
    }
}

static void init_cdc(void)
{
    tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy      = false,
        .configuration_descriptor = NULL,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev        = TINYUSB_USBDEV_0,
        .cdc_port       = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = CONFIG_TINYUSB_CDC_RX_BUFSIZE,
        .callback_rx    = &cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
    ESP_LOGI(TAG, "USB CDC initialized");
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void serial_handler_init(serial_mode_t mode, serial_rx_cb_t rx_cb)
{
    s_mode  = mode;
    s_rx_cb = rx_cb;

    if (mode == SERIAL_MODE_TTL) {
        init_ttl();
    } else {
        init_cdc();
    }
}

void serial_send(const uint8_t *data, size_t len)
{
    if (s_mode == SERIAL_MODE_TTL) {
        uart_write_bytes(TTL_UART_PORT, (const char *)data, len);
    } else {
        /* CDC: send on interface 0 */
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, data, len);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
    }
}
