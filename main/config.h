#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/uart.h"

/* Watchdog defaults */
#define DEFAULT_WATCHDOG_TIMEOUT_MS   (15 * 60 * 1000)  /* 15 minutes */
#define DEFAULT_TURN_OFF_PERIOD_MS    5000               /* 5 seconds  */
#define DEFAULT_MAX_RESTART_ATTEMPTS  5

/* Hardware defaults */
#define DEFAULT_RELAY_PIN    GPIO_NUM_4
#define DEFAULT_LED_PIN      GPIO_NUM_8
#define DEFAULT_BUTTON_PIN   GPIO_NUM_9

/* TTL UART settings */
#define TTL_UART_PORT        UART_NUM_1
#define TTL_UART_TX_PIN      GPIO_NUM_16
#define TTL_UART_RX_PIN      GPIO_NUM_17
#define TTL_UART_BAUD_RATE   115200
#define TTL_UART_BUF_SIZE    256

/* WiFi defaults */
#define DEFAULT_WIFI_SSID      "ESP32-Watchdog"
#define DEFAULT_WIFI_PASSWORD  "watchdog123"
#define DEFAULT_WIFI_HIDDEN    false
#define DEFAULT_WIFI_MAX_STA   4

/* LED blink timing (ms) */
#define LED_BLINK_STANDBY_MS  1000
#define LED_BLINK_WARNING_MS  200
#define LED_BLINK_RESTART_MS  100
#define LED_COMM_OK_MS        1000

/* Warning threshold before watchdog triggers */
#define WATCHDOG_WARNING_BEFORE_MS  (60 * 1000)  /* 1 minute */

/* Serial mode selection */
typedef enum {
    SERIAL_MODE_TTL = 0,
    SERIAL_MODE_CDC = 1,
} serial_mode_t;

/* Persistent device configuration (saved to NVS) */
typedef struct {
    uint32_t watchdog_timeout_ms;
    uint32_t turn_off_period_ms;
    uint8_t  max_restart_attempts;
    uint8_t  relay_pin;
    uint8_t  led_pin;
    uint8_t  button_pin;
    serial_mode_t serial_mode;
    char     wifi_ssid[32];
    char     wifi_password[64];
    bool     wifi_hidden;
} device_config_t;

/* Runtime device state */
typedef struct {
    bool     relay_on;
    uint8_t  restart_attempts;
    int64_t  last_comm_time_ms;   /* esp_timer_get_time() / 1000 */
    bool     watchdog_triggered;
    bool     warning_active;
} device_state_t;
