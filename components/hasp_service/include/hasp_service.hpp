#pragma once

#include <string>
#include <ArduinoJson.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

// hasp_service.hpp (or a small hasp_service_types.hpp)
enum class ServiceMode : uint8_t {
    Never      = 0,  // disabled – never start
    Manual     = 1,  // only via explicit start() / API / command
    Once       = 2,  // start when network is up the first time; do not restart after disconnect
    OnBoot     = 3,  // start as soon as the service is allowed (boot / startAll), independent of IP
    KeepAlive  = 4,  // start on GOT_IP, stop on disconnect, start again on reconnect
};

/**
 * Base class for all openHASP services (wifi, mqtt, http, …).
 *
 * NVS layout:
 *   namespace = name()          e.g. "wifi"
 *   keys      = individual values (ssid, password, hostname, …)
 *
 * get_config / set_config use ArduinoJson only as a convenient
 * transfer/filter format.  Values are stored as separate NVS keys
 * and are written only when they actually change.
 */
class HaspService {
public:
    virtual ~HaspService() = default;

    /** Unique service name – also used as NVS namespace */
    virtual const char* name() const = 0;

    virtual bool isRunning() const = 0;
    virtual esp_err_t start();
    virtual esp_err_t restart();
    virtual esp_err_t stop();
    
    /**
     * Fill doc with the current configuration.
     * Implementations should call the protected nvs_get_* helpers.
     */
    virtual esp_err_t get_config(JsonObject obj) const = 0;

    /**
     * Apply configuration from doc.
     * Only keys present in the document are touched.
     * Implementations should call the protected nvs_set_* helpers
     * (they already skip unchanged values).
     */
    virtual esp_err_t set_config(JsonObjectConst obj) = 0;

    /**
     * Erase ALL configuration keys belonging to this service.
     * Used for factory reset / decommissioning.
     */
    virtual esp_err_t erase_config();

protected:
    ServiceMode mode_ = ServiceMode::Manual;
    bool ran_once_    = false;   // for Once
    bool enabled_runtime_ = true;

    ServiceMode mode() const { return mode_; }

    /** Call from set_config when "mode" is present */
    void apply_mode_from_config(JsonObjectConst svc);

    /**
     * Network events – default implementation encodes the policy.
     * Concrete services override start_backend() / stop_backend().
     */
    virtual void on_network_up() = 0;
    virtual void on_network_down() = 0;

    /** Real work: httpd_start, mqtt_start, … */
    virtual esp_err_t start_backend() = 0;
    virtual esp_err_t stop_backend() = 0;

    virtual void log_memory()
    {
        ESP_LOGW("MEM",
                "%s: internal=%u, block=%u, psram=%u, total=%u",
                name(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    }
    // ------------------------------------------------------------------
    // NVS helpers – namespace = name()
    // ------------------------------------------------------------------

    /** Read a string key.  Returns ESP_ERR_NVS_NOT_FOUND if missing. */
    esp_err_t nvs_get_string(const char* key, std::string& out) const;

    /**
     * Write a string key only if the value is different from the
     * currently stored one (or the key does not exist yet).
     */
    esp_err_t nvs_set_string(const char* key, const std::string& value) const;

    /** Read a uint32 key. */
    esp_err_t nvs_get_u32(const char* key, uint32_t& out) const;

    /** Write a uint32 key only if different. */
    esp_err_t nvs_set_u32(const char* key, uint32_t value) const;

    /** Read a bool (stored as u8 0/1). */
    esp_err_t nvs_get_bool(const char* key, bool& out) const;

    /** Write a bool only if different. */
    esp_err_t nvs_set_bool(const char* key, bool value) const;

    /** Erase a single key (optional helper). */
    esp_err_t nvs_erase_key(const char* key) const;
};