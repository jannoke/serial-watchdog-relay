#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "nvs_storage.h"
#include "led_status.h"
#include "relay_controller.h"
#include "serial_handler.h"
#include "serial_command.h"
#include "wifi_ap.h"
#include "watchdog_timer.h"
#include "web_server.h"
#include "oled_display.h"

static const char *TAG = "main";

static device_config_t s_config;

/* ── Watchdog trigger callback ───────────────────────────────────────────── */

static void on_watchdog_trigger(void)
{
    ESP_LOGW(TAG, "Watchdog fired – cycling relay for %lu ms",
             (unsigned long)s_config.turn_off_period_ms);
    relay_cycle(s_config.turn_off_period_ms);
    led_status_set(LED_STATE_STANDBY);
}

/* ── Serial RX callback ──────────────────────────────────────────────────── */

static void on_serial_rx(const uint8_t *data, size_t len)
{
    serial_command_process(data, len);
}

/* ── OLED display refresh task ───────────────────────────────────────────── */

static void oled_task(void *arg)
{
    /* 10 Hz refresh – smooth blinking animations */
    const TickType_t period = pdMS_TO_TICKS(100);
    TickType_t last_wake    = xTaskGetTickCount();

    while (1) {
        oled_display_update();
        vTaskDelayUntil(&last_wake, period);
    }
}

/* ── Button handling ─────────────────────────────────────────────────────── */

#define BUTTON_LONG_PRESS_MS  3000
#define BUTTON_DEBOUNCE_MS    50

static void button_task(void *arg)
{
    uint8_t pin = s_config.button_pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    int64_t press_start = 0;
    bool    pressed     = false;

    while (1) {
        int level = gpio_get_level(pin);

        if (level == 0 && !pressed) {
            /* Button pressed (active-low) */
            vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
            if (gpio_get_level(pin) == 0) {
                pressed     = true;
                press_start = esp_timer_get_time() / 1000LL;
            }
        } else if (level == 1 && pressed) {
            /* Button released */
            int64_t duration = (esp_timer_get_time() / 1000LL) - press_start;
            pressed = false;

            if (duration >= BUTTON_LONG_PRESS_MS) {
                ESP_LOGI(TAG, "Long press detected – resetting restart attempts");
                watchdog_reset_attempts();
            } else {
                ESP_LOGI(TAG, "Short press detected – resetting watchdog timer");
                watchdog_communication_received();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-C6 Watchdog Relay starting");

    /* 1. NVS */
    ESP_ERROR_CHECK(nvs_storage_init());

    /* 2. Load configuration (fills defaults if NVS is empty) */
    nvs_storage_load_config(&s_config);

    /* 3. LED */
    led_status_init(s_config.led_pin);
    led_status_set(LED_STATE_STANDBY);

    /* 4. Relay – start ON (device powered) */
    relay_init(s_config.relay_pin);
    relay_on();

    /* 5. Serial command handler */
    serial_command_init(&s_config);

    /* 6. Serial */
    serial_handler_init(s_config.serial_mode, on_serial_rx);

    /* 7. WiFi AP */
    wifi_ap_init(s_config.wifi_ssid, s_config.wifi_password,
                 s_config.wifi_hidden, DEFAULT_WIFI_MAX_STA);

    /* 8. Web server */
    web_server_start(&s_config);

    /* 9. Watchdog timer */
    watchdog_timer_init(s_config.watchdog_timeout_ms,
                        s_config.max_restart_attempts,
                        on_watchdog_trigger);

    /* 10. OLED display */
    if (s_config.oled_enabled) {
        oled_display_init(s_config.oled_sda_pin,
                          s_config.oled_scl_pin,
                          s_config.oled_i2c_addr);
        xTaskCreate(oled_task, "oled", 4096, NULL, 2, NULL);
    }

    /* 11. Physical button */
    xTaskCreate(button_task, "button", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "All modules initialized");
}
