#include "nvs_storage.h"

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "nvs_storage";

#define NVS_NAMESPACE  "watchdog"
#define NVS_KEY_CFG    "device_cfg"
#define NVS_KEY_ATTEMPTS "restart_cnt"

esp_err_t nvs_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erasing, erasing now");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void fill_defaults(device_config_t *config)
{
    config->watchdog_timeout_ms  = DEFAULT_WATCHDOG_TIMEOUT_MS;
    config->turn_off_period_ms   = DEFAULT_TURN_OFF_PERIOD_MS;
    config->max_restart_attempts = DEFAULT_MAX_RESTART_ATTEMPTS;
    config->relay_pin            = DEFAULT_RELAY_PIN;
    config->led_pin              = DEFAULT_LED_PIN;
    config->button_pin           = DEFAULT_BUTTON_PIN;
    config->serial_mode          = SERIAL_MODE_CDC;
    strlcpy(config->wifi_ssid,     DEFAULT_WIFI_SSID,     sizeof(config->wifi_ssid));
    strlcpy(config->wifi_password, DEFAULT_WIFI_PASSWORD, sizeof(config->wifi_password));
    config->wifi_hidden = DEFAULT_WIFI_HIDDEN;
}

esp_err_t nvs_storage_load_config(device_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config, using defaults");
        fill_defaults(config);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        fill_defaults(config);
        return err;
    }

    size_t required_size = sizeof(device_config_t);
    err = nvs_get_blob(handle, NVS_KEY_CFG, config, &required_size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND || required_size != sizeof(device_config_t)) {
        ESP_LOGI(TAG, "Config blob missing or size mismatch, using defaults");
        fill_defaults(config);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob failed: %s", esp_err_to_name(err));
        fill_defaults(config);
    }
    return ESP_OK;
}

esp_err_t nvs_storage_save_config(const device_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY_CFG, config, sizeof(device_config_t));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Config saved");
    }
    return err;
}

esp_err_t nvs_storage_get_restart_attempts(uint8_t *attempts)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *attempts = 0;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        *attempts = 0;
        return err;
    }

    uint8_t val = 0;
    err = nvs_get_u8(handle, NVS_KEY_ATTEMPTS, &val);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *attempts = 0;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_u8 failed: %s", esp_err_to_name(err));
        *attempts = 0;
        return err;
    }
    *attempts = val;
    return ESP_OK;
}

esp_err_t nvs_storage_increment_restart_attempts(void)
{
    uint8_t current = 0;
    nvs_storage_get_restart_attempts(&current);
    current++;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_u8(handle, NVS_KEY_ATTEMPTS, current);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "Restart attempts: %d", current);
    return err;
}

esp_err_t nvs_storage_reset_restart_attempts(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_u8(handle, NVS_KEY_ATTEMPTS, 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "Restart attempts reset to 0");
    return err;
}
