#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Callback invoked when the watchdog triggers a relay cycle.
 *
 * The implementation is responsible for calling relay_cycle() with the
 * configured off-period and for updating the LED state.
 */
typedef void (*watchdog_trigger_cb_t)(void);

/**
 * @brief Initialize the watchdog timer.
 *
 * @param timeout_ms          Inactivity timeout in milliseconds.
 * @param max_attempts        Maximum relay-cycle attempts before giving up.
 * @param trigger_cb          Callback invoked when the watchdog fires.
 */
void watchdog_timer_init(uint32_t timeout_ms,
                         uint8_t  max_attempts,
                         watchdog_trigger_cb_t trigger_cb);

/**
 * @brief Record a successful communication event.
 *
 * Resets the watchdog countdown and clears the restart attempt counter.
 */
void watchdog_communication_received(void);

/**
 * @brief Get the number of milliseconds remaining before the watchdog fires.
 *
 * @return Remaining milliseconds, or 0 if already triggered.
 */
int64_t watchdog_get_remaining_ms(void);

/**
 * @brief Get the timestamp (ms since boot) of the last received communication.
 *
 * @return Millisecond timestamp, or 0 if no communication has been received.
 */
int64_t watchdog_get_last_comm_ms(void);

/**
 * @brief Get the current restart attempt count (from NVS).
 *
 * @return Number of restart attempts performed since last reset.
 */
uint8_t watchdog_get_restart_attempts(void);

/**
 * @brief Reset the restart attempt counter to zero (in NVS and in memory).
 */
void watchdog_reset_attempts(void);

/**
 * @brief Update the watchdog timeout.
 *
 * The new value takes effect immediately.
 *
 * @param timeout_ms  New timeout in milliseconds.
 */
void watchdog_set_timeout(uint32_t timeout_ms);

/**
 * @brief Update the maximum restart attempts.
 *
 * @param max_attempts  New maximum.
 */
void watchdog_set_max_attempts(uint8_t max_attempts);

/**
 * @brief Get the configured watchdog timeout in milliseconds.
 *
 * @return Timeout in milliseconds.
 */
uint32_t watchdog_get_timeout_ms(void);

/**
 * @brief Get the configured maximum restart attempts.
 *
 * @return Maximum restart attempts.
 */
uint8_t watchdog_get_max_attempts(void);
