#include "watchdog_timer.h"

#include "nvs_storage.h"
#include "led_status.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "watchdog";

static uint32_t               s_timeout_ms   = DEFAULT_WATCHDOG_TIMEOUT_MS;
static uint8_t                s_max_attempts = DEFAULT_MAX_RESTART_ATTEMPTS;
static watchdog_trigger_cb_t  s_trigger_cb   = NULL;
static int64_t                s_last_comm_ms = 0;
static uint8_t                s_attempts     = 0;
static bool                   s_halted       = false;
static TaskHandle_t           s_task_handle  = NULL;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000LL;
}

static void watchdog_task(void *arg)
{
    /* Record boot time as initial "last communication" baseline */
    s_last_comm_ms = now_ms();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        if (s_halted) {
            continue;
        }

        int64_t elapsed  = now_ms() - s_last_comm_ms;
        int64_t remaining = (int64_t)s_timeout_ms - elapsed;

        /* Warning LED: 1 minute before trigger */
        if (remaining > 0 && remaining <= (int64_t)WATCHDOG_WARNING_BEFORE_MS) {
            if (led_status_get() == LED_STATE_STANDBY) {
                led_status_set(LED_STATE_WARNING);
            }
        } else if (remaining > (int64_t)WATCHDOG_WARNING_BEFORE_MS) {
            if (led_status_get() == LED_STATE_WARNING) {
                led_status_set(LED_STATE_STANDBY);
            }
        }

        if (remaining <= 0) {
            /* Check attempt limit */
            if (s_attempts >= s_max_attempts) {
                ESP_LOGW(TAG, "Max restart attempts (%d) reached – halting", s_max_attempts);
                led_status_set(LED_STATE_STANDBY);
                s_halted = true;
                continue;
            }

            ESP_LOGW(TAG, "Watchdog triggered! Attempt %d/%d",
                     s_attempts + 1, s_max_attempts);

            nvs_storage_increment_restart_attempts();
            nvs_storage_get_restart_attempts(&s_attempts);

            led_status_set(LED_STATE_RESTART_ACTIVE);

            if (s_trigger_cb) {
                s_trigger_cb();
            }

            /* Reset the countdown after the relay cycle completes */
            s_last_comm_ms = now_ms();
        }
    }
}

void watchdog_timer_init(uint32_t timeout_ms,
                         uint8_t  max_attempts,
                         watchdog_trigger_cb_t trigger_cb)
{
    s_timeout_ms   = timeout_ms;
    s_max_attempts = max_attempts;
    s_trigger_cb   = trigger_cb;

    nvs_storage_get_restart_attempts(&s_attempts);
    ESP_LOGI(TAG, "Watchdog init: timeout=%lu ms, max_attempts=%d, current_attempts=%d",
             (unsigned long)timeout_ms, max_attempts, s_attempts);

    xTaskCreate(watchdog_task, "watchdog", 4096, NULL, 4, &s_task_handle);
}

void watchdog_communication_received(void)
{
    s_last_comm_ms = now_ms();
    s_halted       = false;

    /* Reset NVS and in-memory attempts */
    nvs_storage_reset_restart_attempts();
    s_attempts = 0;

    led_status_set(LED_STATE_COMM_OK);
    ESP_LOGI(TAG, "Communication received – watchdog reset");
}

int64_t watchdog_get_remaining_ms(void)
{
    int64_t elapsed   = now_ms() - s_last_comm_ms;
    int64_t remaining = (int64_t)s_timeout_ms - elapsed;
    return remaining > 0 ? remaining : 0;
}

int64_t watchdog_get_last_comm_ms(void)
{
    return s_last_comm_ms;
}

uint8_t watchdog_get_restart_attempts(void)
{
    return s_attempts;
}

void watchdog_reset_attempts(void)
{
    nvs_storage_reset_restart_attempts();
    s_attempts = 0;
    s_halted   = false;
    ESP_LOGI(TAG, "Restart attempts reset");
}

void watchdog_set_timeout(uint32_t timeout_ms)
{
    s_timeout_ms = timeout_ms;
}

void watchdog_set_max_attempts(uint8_t max_attempts)
{
    s_max_attempts = max_attempts;
}
