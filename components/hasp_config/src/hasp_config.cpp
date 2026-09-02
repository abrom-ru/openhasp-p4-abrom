#include "hasp_config.hpp"

#include "hasp_service_manager.hpp"
#include "hasp_fs.hpp"

#include <ArduinoJson.h>
#include "esp_log.h"

#include <cstdio>
#include <string>
#include <sys/stat.h>

static const char* TAG = "HASP_CONF";

// 8 KiB matches S3 MAX_CONFIG_JSON_ALLOC_SIZE (see hasp_conf.h). Anything
// larger is almost certainly a corrupted file, not a legitimate config.
static constexpr size_t CONFIG_MAX_SIZE = 8192;

static std::string default_path()
{
    std::string p = hasp_fs_mount_point();
    p += "/config.json";
    return p;
}

esp_err_t hasp_config_load(ServiceManager& mgr, const char* path)
{
    std::string owned;
    if (!path) {
        owned = default_path();
        path  = owned.c_str();
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "config load: %s not found — services fall back to NVS", path);
        return ESP_ERR_NOT_FOUND;
    }

    struct stat st;
    size_t size = 0;
    if (fstat(fileno(f), &st) == 0 && st.st_size > 0) {
        size = (size_t)st.st_size;
    }
    if (size == 0 || size > CONFIG_MAX_SIZE) {
        ESP_LOGE(TAG, "config load: bad size %u", (unsigned)size);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    std::string buf(size, '\0');
    size_t n = fread(buf.data(), 1, size, f);
    fclose(f);
    if (n != size) {
        ESP_LOGE(TAG, "config load: short read %u/%u", (unsigned)n, (unsigned)size);
        return ESP_FAIL;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err) {
        ESP_LOGE(TAG, "config load: deserialize failed: %s", err.c_str());
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "config load: %s (%u bytes) -> set_config", path, (unsigned)size);
    return mgr.set_config(doc.as<JsonObjectConst>());
}

esp_err_t hasp_config_save(ServiceManager& mgr, const char* path)
{
    std::string owned;
    if (!path) {
        owned = default_path();
        path  = owned.c_str();
    }

    JsonDocument doc;
    JsonObject   obj = doc.to<JsonObject>();
    esp_err_t err = mgr.get_config(obj);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config save: get_config failed: %s", esp_err_to_name(err));
        return err;
    }

    std::string body;
    body.reserve(512);
    size_t n = serializeJson(doc, body);
    if (n == 0) {
        ESP_LOGE(TAG, "config save: empty serialization");
        return ESP_FAIL;
    }

    FILE* f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "config save: cannot open %s for write", path);
        return ESP_FAIL;
    }

    size_t w = fwrite(body.data(), 1, body.size(), f);
    fclose(f);
    if (w != body.size()) {
        ESP_LOGE(TAG, "config save: short write %u/%u", (unsigned)w, (unsigned)body.size());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "config save: %s (%u bytes)", path, (unsigned)w);
    return ESP_OK;
}
