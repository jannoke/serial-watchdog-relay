#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the relay GPIO.
 *
 * The relay is turned OFF after initialization.
 *
 * @param gpio_pin  GPIO number connected to the relay control input.
 */
void relay_init(uint8_t gpio_pin);

/** @brief Energize the relay (turn monitored device ON). */
void relay_on(void);

/** @brief De-energize the relay (turn monitored device OFF). */
void relay_off(void);

/**
 * @brief Cycle the relay: turn OFF for @p off_duration_ms, then turn ON.
 *
 * The function blocks for the duration of the off period.
 *
 * @param off_duration_ms  Duration (ms) to keep the relay OFF.
 */
void relay_cycle(uint32_t off_duration_ms);

/**
 * @brief Check whether the relay is currently energized.
 *
 * @return true if the relay is ON, false otherwise.
 */
bool relay_is_on(void);
