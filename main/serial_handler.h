#pragma once

#include <stdint.h>
#include <stddef.h>
#include "config.h"

/**
 * @brief Callback type invoked when data is received over the serial port.
 *
 * @param data  Pointer to received bytes.
 * @param len   Number of bytes received.
 */
typedef void (*serial_rx_cb_t)(const uint8_t *data, size_t len);

/**
 * @brief Initialize the serial handler.
 *
 * Starts either a USB CDC listener or a TTL UART listener depending on
 * @p mode.  The supplied @p rx_cb is called from the reader task whenever
 * data arrives.
 *
 * @param mode   Communication mode (TTL or CDC).
 * @param rx_cb  Callback invoked on received data (may be NULL).
 */
void serial_handler_init(serial_mode_t mode, serial_rx_cb_t rx_cb);

/**
 * @brief Send bytes over the active serial interface.
 *
 * @param data  Pointer to data to send.
 * @param len   Number of bytes to send.
 */
void serial_send(const uint8_t *data, size_t len);
