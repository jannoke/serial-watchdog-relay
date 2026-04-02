#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the OLED display (SSD1306, 128x32, I2C).
 *
 * Sets up the I2C master bus and configures the SSD1306 controller.
 * Must be called once at startup before oled_display_update().
 *
 * @param sda_pin   GPIO number for I2C SDA.
 * @param scl_pin   GPIO number for I2C SCL.
 * @param i2c_addr  7-bit I2C address of the SSD1306 (typically 0x3C).
 */
void oled_display_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t i2c_addr);

/**
 * @brief Refresh the display with the current device state.
 *
 * Should be called periodically (e.g. at 10 Hz) so that the heartbeat
 * blink animation and countdown timer update smoothly.
 *
 * Line 1: remaining watchdog time / total timeout  (e.g. "5:30 / 15:00")
 * Line 2: attempts / max, WiFi icon, WiFi-connected icon, heartbeat icon
 */
void oled_display_update(void);

/**
 * @brief Update the WiFi client-connected flag shown on line 2.
 *
 * @param connected  true if at least one station is associated to the AP.
 */
void oled_display_set_wifi_connected(bool connected);

/**
 * @brief Signal that a heartbeat (PING / RESET_TIMER) was received.
 *
 * Illuminates the heart icon for 1 second and records the time so that
 * the icon is shown whenever a heartbeat arrived in the last 60 seconds.
 */
void oled_display_heartbeat_received(void);
