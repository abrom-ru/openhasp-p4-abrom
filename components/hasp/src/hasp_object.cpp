/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (3a-refactor): structure of hasp_new_object() mirrors
 * src/hasp/hasp_object.cpp:228 — page selection, id lookup, sdbm-based type
 * switch, post-create ops, then hasp_parse_json_attributes(). LVGL 7/8 API
 * calls swapped for LVGL 9 equivalents (documented per-line). Attribute
 * processor is a stub covering only x/y/w/h/text/val — extended in 3e.
 * Object registry (hasp_find_obj_from_parent_id / haspPages) lands in 3b/3c;
 * for now parent = lv_screen_active() and re-creation is not detected.
 */

#include "hasp_object.h"
#include "hasp_event.h"
#include "hasp_parser.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"

static const char* TAG = "hasp_obj";

/* Stub attribute processor — 3a scope: x, y, w, h, text, val.
 * Mirrors call site src/hasp/hasp_object.cpp:743 (hasp_parse_json_attributes).
 * Real per-attribute switch by SDBM lives in hasp_attribute.cpp (S3) — comes in 3e.
 */
static void hasp_process_obj_attribute_3a(lv_obj_t* obj, const char* attr, JsonVariantConst value)
{
    if (!attr || !obj) return;

    if      (!strcasecmp(attr, "x"))    lv_obj_set_x(obj, value.as<int32_t>());
    else if (!strcasecmp(attr, "y"))    lv_obj_set_y(obj, value.as<int32_t>());
    else if (!strcasecmp(attr, "w"))    lv_obj_set_width(obj, value.as<int32_t>());
    else if (!strcasecmp(attr, "h"))    lv_obj_set_height(obj, value.as<int32_t>());
    else if (!strcasecmp(attr, "text")) {
        const char* txt = value.as<const char*>();
        if (!txt) return;
        switch (obj_get_type(obj)) {
            case LV_HASP_LABEL:
                lv_label_set_text(obj, txt);
                break;
            case LV_HASP_CHECKBOX:
                lv_checkbox_set_text(obj, txt);
                break;
            case LV_HASP_BUTTON: {
                /* First (or only) child of a HASP button is its label — see
                 * button creation branch below where lv_label_create(obj) is called. */
                lv_obj_t* lbl = lv_obj_get_child(obj, 0);
                if (lbl) lv_label_set_text(lbl, txt);
                break;
            }
            default: break;
        }
    }
    else if (!strcasecmp(attr, "val")) {
        int32_t v = value.as<int32_t>();
        switch (obj_get_type(obj)) {
            case LV_HASP_SLIDER:
                lv_slider_set_value(obj, v, LV_ANIM_OFF);
                break;
            case LV_HASP_BAR:
                lv_bar_set_value(obj, v, LV_ANIM_OFF);
                break;
            case LV_HASP_SWITCH:
            case LV_HASP_CHECKBOX:
                if (v) lv_obj_add_state(obj, LV_STATE_CHECKED);
                else   lv_obj_remove_state(obj, LV_STATE_CHECKED);
                break;
            default: break;
        }
    }
    /* Unknown attributes silently ignored in 3a (S3 logs a warning via
     * hasp_attribute — added in 3e). */
}

static int hasp_parse_json_attributes(lv_obj_t* obj, JsonObjectConst doc)
{
    int i = 0;
    for (JsonPairConst kv : doc) {
        hasp_process_obj_attribute_3a(obj, kv.key().c_str(), kv.value());
        i++;
    }
    return i;
}

/* Mirrors src/hasp/hasp_object.cpp:228 hasp_new_object().
 *
 * Differences (documented so future me can grep):
 *   - haspPages.get_obj(pageid) → lv_screen_active(). Page system lands in 3b.
 *   - hasp_find_obj_from_parent_id() lookup → not implemented; every call creates
 *     a new object (registry lands in 3c).
 *   - parentid / groupid handling stubbed — fields consumed and ignored.
 *   - LVGL 7/8 create APIs (`lv_btn_create(parent, NULL)`) → LVGL 9 (`lv_button_create(parent)`).
 *   - obj->user_data.objid = ... → allocated hasp_obj_user_data_t + lv_obj_set_user_data.
 *   - lv_obj_set_event_cb(obj, cb) → lv_obj_add_event_cb(obj, cb, LV_EVENT_ALL, NULL).
 *   - lv_obj_add_protect(PRESS_LOST) — default behavior in LVGL 9, dropped.
 *   - lv_obj_set_gesture_parent(false) → lv_obj_remove_flag(GESTURE_BUBBLE).
 *   - lv_obj_set_click(true) — default in LVGL 9, dropped.
 *   - lv_label_set_recolor(true) — recolor removed in LVGL 9, dropped
 *     (label recolor comes via spans/styles later).
 */
void hasp_new_object(const JsonObject& config, uint8_t& saved_page_id)
{
    /* Skip line detection */
    if (!config[FP_SKIP].isNull() && config[FP_SKIP].as<bool>()) return;

    /* Page selection — 3a: single active screen; pageid saved but not used yet. */
    uint8_t pageid = saved_page_id;
    if (!config[FP_PAGE].isNull()) {
        pageid = config[FP_PAGE].as<uint8_t>();
    }
    saved_page_id = pageid;

    lv_obj_t* parent_obj = lv_screen_active();
    if (!parent_obj) {
        ESP_LOGW(TAG, "no active screen");
        return;
    }

    uint8_t id  = config[FP_ID].as<uint8_t>();
    uint16_t sdbm = 0;

    if (config[FP_OBJ].isNull()) {
        return; /* comment line */
    }
    sdbm = Parser::get_sdbm(config[FP_OBJ].as<const char*>());

    lv_obj_t* obj    = nullptr;
    lv_event_cb_t cb = nullptr;
    uint8_t objid    = 0;

    switch (sdbm) {

        /* ----- Basic Objects ------ */
        case LV_HASP_BUTTON:
        case HASP_OBJ_BTN: {
            obj = lv_button_create(parent_obj);
            if (obj) {
                lv_obj_t* lbl = lv_label_create(obj);
                if (lbl) {
                    lv_label_set_text(lbl, "");
                    lv_obj_center(lbl); /* LVGL 9 replacement of lv_obj_align(NULL, CENTER, 0, 0) */
                    /* Tag child label so future attribute lookups can find it. */
                    hasp_obj_user_data_t* lud = (hasp_obj_user_data_t*)calloc(1, sizeof(*lud));
                    if (lud) { lud->objid = LV_HASP_LABEL; lv_obj_set_user_data(lbl, lud); }
                }
                cb    = generic_event_handler;
                objid = LV_HASP_BUTTON;
            }
            break;
        }

        case LV_HASP_CHECKBOX:
        case HASP_OBJ_CHECKBOX: {
            obj = lv_checkbox_create(parent_obj);
            if (obj) {
                cb    = toggle_event_handler;
                objid = LV_HASP_CHECKBOX;
            }
            break;
        }

        case LV_HASP_LABEL:
        case HASP_OBJ_LABEL: {
            obj = lv_label_create(parent_obj);
            if (obj) {
                lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_CLIP); /* LVGL 9 name of LV_LABEL_LONG_CROP */
                cb    = generic_event_handler;
                objid = LV_HASP_LABEL;
            }
            break;
        }

        /* ----- Range Objects ------ */
        case LV_HASP_SLIDER:
        case HASP_OBJ_SLIDER: {
            obj = lv_slider_create(parent_obj);
            if (obj) {
                lv_slider_set_range(obj, 0, 100);
                cb    = slider_event_handler;
                objid = LV_HASP_SLIDER;
            }
            break;
        }

        case LV_HASP_BAR:
        case HASP_OBJ_BAR: {
            obj = lv_bar_create(parent_obj);
            if (obj) {
                lv_bar_set_range(obj, 0, 100);
                cb    = generic_event_handler;
                objid = LV_HASP_BAR;
            }
            break;
        }

        /* ----- On/Off Objects ------ */
        case LV_HASP_SWITCH:
        case HASP_OBJ_SWITCH: {
            obj = lv_switch_create(parent_obj);
            if (obj) {
                cb    = toggle_event_handler;
                objid = LV_HASP_SWITCH;
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "unsupported obj sdbm=%u (3a supports btn/label/switch/slider/checkbox/bar)", sdbm);
            return;
    }

    if (!obj) {
        ESP_LOGE(TAG, "object create failed for id=%u", id);
        return;
    }

    /* Post-create common ops — mirrors src/hasp/hasp_object.cpp:706. */
    /* LV_PROTECT_PRESS_LOST: default in LVGL 9, no-op. */
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE); /* was lv_obj_set_gesture_parent(false) */
    /* CLICKABLE flag is default-on for interactive widgets in LVGL 9. */

    /* Allocate + stash HASP user_data (LVGL 9 adaptation of obj->user_data.id/.objid). */
    hasp_obj_user_data_t* ud = (hasp_obj_user_data_t*)calloc(1, sizeof(*ud));
    if (!ud) {
        ESP_LOGE(TAG, "user_data alloc failed");
        return;
    }
    ud->id      = id;
    ud->objid   = objid;
    ud->groupid = config[FP_GROUPID].isNull() ? 0 : config[FP_GROUPID].as<uint8_t>();
    lv_obj_set_user_data(obj, ud);

    /* Wire the per-type event handler + a shared delete hook to free user_data. */
    lv_obj_add_event_cb(obj, cb, LV_EVENT_ALL, nullptr);

    ESP_LOGI(TAG, "created p%ub%u objid=%u sdbm=%u", pageid, id, objid, sdbm);

    /* Apply remaining attributes (x/y/w/h/text/val in 3a scope).
     * Same pattern as S3: remove framework keys, then iterate the rest.
     * JsonObject::remove is const-qualified in ArduinoJson (mutates doc, not handle). */
    config.remove(FP_SKIP);
    config.remove(FP_PAGE);
    config.remove(FP_ID);
    config.remove(FP_OBJ);
    config.remove(FP_PARENTID);
    config.remove(FP_GROUPID);
    hasp_parse_json_attributes(obj, config);
}
