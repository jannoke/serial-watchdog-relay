#pragma once

#include <stdint.h>
#include "config.h"

/**
 * @brief Initialize the serial command handler.
 *
 * Must be called once at startup after NVS and subsystem initialization.
 *
 * @param config  Pointer to the device configuration struct (used for GET/SET commands).
 */
void serial_command_init(device_config_t *config);

/**
 * @brief Process incoming serial bytes.
 *
 * Accumulates bytes into an internal line buffer and processes complete lines
 * (terminated with '\n' or '\r\n') as commands.  Should be called from the
 * serial RX callback.
 *
 * @param data  Pointer to received bytes.
 * @param len   Number of bytes.
 */
void serial_command_process(const uint8_t *data, size_t len);
