#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the WiFi soft-AP.
 *
 * Starts the DHCP server and begins advertising the access point so that
 * clients can connect and reach the built-in web server.
 *
 * @param ssid      Network name (max 31 characters + NUL).
 * @param password  WPA2 passphrase (8-63 characters) or "" for open network.
 * @param hidden    true to hide the SSID beacon.
 * @param max_sta   Maximum number of simultaneous stations (1-4).
 */
void wifi_ap_init(const char *ssid, const char *password,
                  bool hidden, uint8_t max_sta);
