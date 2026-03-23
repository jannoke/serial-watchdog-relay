#include "relay_controller.h"

#include "config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "relay_ctrl";

static uint8_t s_relay_pin = DEFAULT_RELAY_PIN;
static bool    s_relay_on  = false;

void relay_init(uint8_t gpio_pin)
{
    s_relay_pin = gpio_pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Start with relay OFF */
    gpio_set_level(gpio_pin, 0);
    s_relay_on = false;
    ESP_LOGI(TAG, "Relay initialized on GPIO %d (OFF)", gpio_pin);
}

void relay_on(void)
{
    gpio_set_level(s_relay_pin, 1);
    s_relay_on = true;
    ESP_LOGI(TAG, "Relay ON");
}

void relay_off(void)
{
    gpio_set_level(s_relay_pin, 0);
    s_relay_on = false;
    ESP_LOGI(TAG, "Relay OFF");
}

void relay_cycle(uint32_t off_duration_ms)
{
    ESP_LOGI(TAG, "Relay cycle: OFF for %lu ms", (unsigned long)off_duration_ms);
    relay_off();
    vTaskDelay(pdMS_TO_TICKS(off_duration_ms));
    relay_on();
    ESP_LOGI(TAG, "Relay cycle complete: back ON");
}

bool relay_is_on(void)
{
    return s_relay_on;
}
