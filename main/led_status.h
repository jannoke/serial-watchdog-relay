#pragma once

#include <stdint.h>

/**
 * @brief LED status states.
 */
typedef enum {
    LED_STATE_STANDBY,        /**< Slow blink 1 s – device booted, monitoring */
    LED_STATE_WARNING,        /**< Fast blink 200 ms – 1 min before trigger  */
    LED_STATE_RESTART_ACTIVE, /**< Very fast blink 100 ms – relay cycle       */
    LED_STATE_COMM_OK,        /**< Solid ON for 1 s then back to STANDBY      */
} led_state_t;

/**
 * @brief Initialize the LED GPIO and start the blink task.
 *
 * @param gpio_pin  GPIO number of the status LED.
 */
void led_status_init(uint8_t gpio_pin);

/**
 * @brief Set the current LED state.
 *
 * @param state  Desired LED state.
 */
void led_status_set(led_state_t state);

/**
 * @brief Get the current LED state.
 *
 * @return Current led_state_t value.
 */
led_state_t led_status_get(void);
