#pragma once

#include "config.h"

/**
 * @brief Start the HTTP web server.
 *
 * The server exposes a status page and a JSON REST API for reading and
 * writing the device configuration and controlling the watchdog.
 *
 * @param config  Pointer to the current device configuration (read/write).
 */
void web_server_start(device_config_t *config);
