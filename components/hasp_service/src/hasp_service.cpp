#include "hasp_service.hpp"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "HASP_SVC";

// ---------------------------------------------------------------------------
// String
// ---------------------------------------------------------------------------

esp_err_t HaspService::nvs_get_string(const char* key, std::string& out) const
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(name(), NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t len = 0;
    err = ::nvs_get_str(h, key, nullptr, &len);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }

    out.resize(len);
    err = ::nvs_get_str(h, key, out.data(), &len);
    nvs_close(h);

    if (err == ESP_OK && len > 0) {
        out.resize(len - 1);          // strip the null terminator
    }
    return err;
}

esp_err_t HaspService::nvs_set_string(const char* key, const std::string& value) const
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(name(), NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    // Read current value (if any)
    size_t len = 0;
    err = ::nvs_get_str(h, key, nullptr, &len);
    if (err == ESP_OK && len > 0) {
        std::string current(len, '\0');
        nvs_get_str(h, key, current.data(), &len);
        current.resize(len - 1);

        if (current == value) {       // identical → nothing to do
            nvs_close(h);
            return ESP_OK;
        }
    }
    // ESP_ERR_NVS_NOT_FOUND is fine – we will create the key

    err = ::nvs_set_str(h, key, value.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

// ---------------------------------------------------------------------------
// uint32
// ---------------------------------------------------------------------------

esp_err_t HaspService::nvs_get_u32(const char* key, uint32_t& out) const
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(name(), NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    err = ::nvs_get_u32(h, key, &out);
    nvs_close(h);
    return err;
}

esp_err_t HaspService::nvs_set_u32(const char* key, uint32_t value) const
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(name(), NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    uint32_t current = 0;
    err = ::nvs_get_u32(h, key, &current);
    if (err == ESP_OK && current == value) {
        nvs_close(h);
        return ESP_OK;                // unchanged
    }

    err = ::nvs_set_u32(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

// ---------------------------------------------------------------------------
// bool (stored as uint8_t 0/1)
// ---------------------------------------------------------------------------

esp_err_t HaspService::nvs_get_bool(const char* key, bool& out) const
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(name(), NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    uint8_t v = 0;
    err = nvs_get_u8(h, key, &v);
    nvs_close(h);
    if (err == ESP_OK) {
        out = (v != 0);
    }
    return err;
}

esp_err_t HaspService::nvs_set_bool(const char* key, bool value) const
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(name(), NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    uint8_t current = 0;
    err = nvs_get_u8(h, key, &current);
    uint8_t newval = value ? 1 : 0;

    if (err == ESP_OK && current == newval) {
        nvs_close(h);
        return ESP_OK;
    }

    err = nvs_set_u8(h, key, newval);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

// ---------------------------------------------------------------------------
// Erase
// ---------------------------------------------------------------------------

esp_err_t HaspService::nvs_erase_key(const char* key) const
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(name(), NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = ::nvs_erase_key(h, key);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t HaspService::erase_config()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(name(), NVS_READWRITE, &h);
    if (err != ESP_OK) {
        // Namespace does not exist → already clean
        if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
        return err;
    }

    err = nvs_erase_all(h);          // removes every key in this namespace
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config erased for service '%s'", name());
    } else {
        ESP_LOGE(TAG, "Failed to erase config for '%s': %s", name(), esp_err_to_name(err));
    }
    return err;
}



esp_err_t HaspService::start()
{
    if (mode_ == ServiceMode::Never) return ESP_ERR_INVALID_STATE;
    return start_backend();
}

esp_err_t HaspService::stop()
{
    return stop_backend();
}

esp_err_t HaspService::restart()
{
    stop();
    return start();
}