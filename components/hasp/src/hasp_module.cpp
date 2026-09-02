/* Step 7A: hasp section NVS/config adapter. See hasp_module.hpp. */

#include "hasp_module.hpp"

#include "hasp_theme.h"   // haspThemeId
#include "hasp_page.h"    // haspStartPage, PAGE_START_INDEX

#include "esp_log.h"

static const char* TAG = "HASP_MOD";

esp_err_t HaspModule::get_config(JsonObject obj) const
{
    JsonObject hasp = obj[name()].to<JsonObject>();

    // Report live in-memory values (already loaded from NVS/config.json at boot).
    // S3 haspGetConfig writes the running globals here too (hasp.cpp:795-812).
    hasp["theme"]     = haspThemeId;
    hasp["startpage"] = haspStartPage;
    return ESP_OK;
}

esp_err_t HaspModule::set_config(JsonObjectConst obj)
{
    // ServiceManager routes by obj[name()] but keep the "flat obj" fallback
    // (same pattern as HaspWifi/HaspMqtt::set_config) so a bare hasp object
    // still works if someone calls set_config directly.
    JsonObjectConst hasp = obj[name()] | obj;

    if (hasp["theme"].is<uint32_t>() || hasp["theme"].is<int>()) {
        uint32_t v = hasp["theme"].as<uint32_t>();
        if (v > 5 && v != 8 && v != 9) {
            ESP_LOGW(TAG, "theme %lu out of range, ignoring", (unsigned long)v);
        } else {
            haspThemeId = (uint8_t)v;
            nvs_set_u32("theme", v);
        }
    }

    if (hasp["startpage"].is<uint32_t>() || hasp["startpage"].is<int>()) {
        uint32_t v = hasp["startpage"].as<uint32_t>();
        // S3 haspSetConfig accepts 1..HASP_NUM_PAGES; anything else is a
        // config error we should warn on rather than silently clamp.
        if (v == 0 || v > 12) {
            ESP_LOGW(TAG, "startpage %lu out of range, ignoring", (unsigned long)v);
        } else {
            haspStartPage = (uint8_t)v;
            nvs_set_u32("startpage", v);
        }
    }

    ESP_LOGI(TAG, "config applied: theme=%u startpage=%u",
             (unsigned)haspThemeId, (unsigned)haspStartPage);
    return ESP_OK;
}
