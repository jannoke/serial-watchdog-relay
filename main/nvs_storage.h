#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "config.h"

/**
 * @brief Initialize NVS flash.
 *
 * Must be called once at startup before any other NVS operations.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t nvs_storage_init(void);

/**
 * @brief Load device configuration from NVS.
 *
 * Populates @p config with saved values.  If no saved configuration exists the
 * struct is filled with compile-time defaults.
 *
 * @param[out] config  Pointer to the config struct to populate.
 * @return ESP_OK on success.
 */
esp_err_t nvs_storage_load_config(device_config_t *config);

/**
 * @brief Save device configuration to NVS.
 *
 * @param[in] config  Pointer to the config struct to persist.
 * @return ESP_OK on success.
 */
esp_err_t nvs_storage_save_config(const device_config_t *config);

/**
 * @brief Get the persisted restart attempt counter.
 *
 * @param[out] attempts  Current attempt count.
 * @return ESP_OK on success.
 */
esp_err_t nvs_storage_get_restart_attempts(uint8_t *attempts);

/**
 * @brief Increment and persist the restart attempt counter.
 *
 * @return ESP_OK on success.
 */
esp_err_t nvs_storage_increment_restart_attempts(void);

/**
 * @brief Reset the persisted restart attempt counter to zero.
 *
 * @return ESP_OK on success.
 */
esp_err_t nvs_storage_reset_restart_attempts(void);
