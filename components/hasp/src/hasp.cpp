#include "hasp.hpp"

#include <string.h>

#include <ArduinoJson.h>
#include "esp_log.h"
#include "lvgl.h"

static const char* TAG = "hasp";

// Step-1 event handler: log id + code so we can observe taps.
// Future: forward to dispatch / mqtt.
static void hasp_generic_event_cb(lv_event_t* e)
{
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_event_code_t code = lv_event_get_code(e);

    // Only report a handful of "interesting" codes — LVGL emits DRAW_* etc very often.
    if (code != LV_EVENT_PRESSED &&
        code != LV_EVENT_RELEASED &&
        code != LV_EVENT_CLICKED &&
        code != LV_EVENT_LONG_PRESSED) {
        return;
    }

    uint32_t id = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj));
    ESP_LOGI(TAG, "obj %lu event %d", static_cast<unsigned long>(id), static_cast<int>(code));
}

extern "C" esp_err_t hasp_init(void)
{
    ESP_LOGI(TAG, "hasp_init (step 1 minimum)");
    return ESP_OK;
}

extern "C" esp_err_t hasp_dispatch_jsonl(const char* line)
{
    if (line == nullptr) {
        ESP_LOGE(TAG, "dispatch: null line");
        return ESP_ERR_INVALID_ARG;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        ESP_LOGE(TAG, "dispatch: parse failed: %s", err.c_str());
        return ESP_ERR_INVALID_ARG;
    }

    const char* obj_type = doc["obj"] | static_cast<const char*>(nullptr);
    if (obj_type == nullptr) {
        ESP_LOGE(TAG, "dispatch: missing 'obj' field");
        return ESP_ERR_INVALID_ARG;
    }

    // Step 1 scope: only "btn"
    if (strcasecmp(obj_type, "btn") != 0) {
        ESP_LOGE(TAG, "dispatch: unsupported obj type '%s' (step 1 supports 'btn' only)", obj_type);
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t id  = doc["id"] | 0U;
    int32_t  x   = doc["x"]  | 0;
    int32_t  y   = doc["y"]  | 0;
    int32_t  w   = doc["w"]  | 100;
    int32_t  h   = doc["h"]  | 40;
    const char* text = doc["text"] | "";

    lv_obj_t* parent = lv_screen_active();
    if (parent == nullptr) {
        ESP_LOGE(TAG, "dispatch: no active screen");
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
    lv_obj_add_event_cb(btn, hasp_generic_event_cb, LV_EVENT_ALL, nullptr);

    if (text[0] != '\0') {
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_center(label);
    }

    ESP_LOGI(TAG, "btn id=%lu pos=(%ld,%ld) size=%ldx%ld text=\"%s\"",
             static_cast<unsigned long>(id),
             static_cast<long>(x), static_cast<long>(y),
             static_cast<long>(w), static_cast<long>(h),
             text);

    return ESP_OK;
}
