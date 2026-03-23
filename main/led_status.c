#include "led_status.h"

#include "config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "led_status";

static uint8_t      s_led_pin   = DEFAULT_LED_PIN;
static led_state_t  s_state     = LED_STATE_STANDBY;
static TaskHandle_t s_task_handle = NULL;

static void led_task(void *arg)
{
    while (1) {
        led_state_t state = s_state;

        switch (state) {
        case LED_STATE_STANDBY:
            gpio_set_level(s_led_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(LED_BLINK_STANDBY_MS / 2));
            gpio_set_level(s_led_pin, 0);
            vTaskDelay(pdMS_TO_TICKS(LED_BLINK_STANDBY_MS / 2));
            break;

        case LED_STATE_WARNING:
            gpio_set_level(s_led_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(LED_BLINK_WARNING_MS / 2));
            gpio_set_level(s_led_pin, 0);
            vTaskDelay(pdMS_TO_TICKS(LED_BLINK_WARNING_MS / 2));
            break;

        case LED_STATE_RESTART_ACTIVE:
            gpio_set_level(s_led_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(LED_BLINK_RESTART_MS / 2));
            gpio_set_level(s_led_pin, 0);
            vTaskDelay(pdMS_TO_TICKS(LED_BLINK_RESTART_MS / 2));
            break;

        case LED_STATE_COMM_OK:
            gpio_set_level(s_led_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(LED_COMM_OK_MS));
            /* Return to standby after the comm-ok flash */
            if (s_state == LED_STATE_COMM_OK) {
                s_state = LED_STATE_STANDBY;
            }
            gpio_set_level(s_led_pin, 0);
            break;

        default:
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
    }
}

void led_status_init(uint8_t gpio_pin)
{
    s_led_pin = gpio_pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(gpio_pin, 0);

    xTaskCreate(led_task, "led_task", 2048, NULL, 3, &s_task_handle);
    ESP_LOGI(TAG, "LED initialized on GPIO %d", gpio_pin);
}

void led_status_set(led_state_t state)
{
    s_state = state;
}

led_state_t led_status_get(void)
{
    return s_state;
}
