/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (step 3c/3e): hasp_new_object() mirrors
 * src/hasp/hasp_object.cpp:228 — page selection (via haspPages, step 3b),
 * id lookup via hasp_find_obj_from_parent_id (step 3c), sdbm-based type
 * switch, post-create ops, then hasp_parse_json_attributes().
 *
 * Object finders (hasp_find_obj_from_parent_id / _from_page_id / hasp_find_id_from_obj)
 * ported below — mirrors src/hasp/hasp_object.cpp:22..69.
 *
 * LVGL 7/8 API calls swapped for LVGL 9 equivalents (documented per-line).
 *
 * 3e: attribute processor moved to hasp_attribute.cpp — this file now only
 * holds the thin pageid+objid wrapper (hasp_process_attribute, S3 line 166)
 * and hasp_parse_json_attributes which drives it for each JSON key.
 */

#include "hasp_object.h"
#include "hasp_attribute.h"
#include "hasp_event.h"
#include "hasp_parser.h"
#include "hasp_page.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"

static const char* TAG = "hasp_obj";

/* Mirrors S3 hasp_object.cpp:166 — used by dispatch (`p1b2.text=...`). */
void hasp_process_attribute(uint8_t pageid, uint8_t objid, const char* attr, const char* payload, bool update)
{
    lv_obj_t* obj = hasp_find_obj_from_page_id(pageid, objid);
    if (!obj) {
        ESP_LOGW(TAG, "unknown object p%ub%u", pageid, objid);
        return;
    }
    hasp_process_obj_attribute(obj, attr, payload, update);
}

static int hasp_parse_json_attributes(lv_obj_t* obj, JsonObjectConst doc)
{
    int i = 0;
    /* Small on-stack buffer for numeric-to-string conversion. Sufficient for
     * int/float attrs; strings pass through directly. */
    char buf[32];
    for (JsonPairConst kv : doc) {
        JsonVariantConst v = kv.value();
        const char* payload_str = nullptr;
        if (v.is<const char*>()) {
            payload_str = v.as<const char*>();
        } else if (v.is<int32_t>() || v.is<long>() || v.is<unsigned int>()) {
            snprintf(buf, sizeof(buf), "%ld", (long)v.as<int32_t>());
            payload_str = buf;
        } else if (v.is<float>() || v.is<double>()) {
            snprintf(buf, sizeof(buf), "%g", v.as<double>());
            payload_str = buf;
        } else if (v.is<bool>()) {
            payload_str = v.as<bool>() ? "1" : "0";
        }
        if (payload_str) {
            hasp_process_obj_attribute(obj, kv.key().c_str(), payload_str, true);
        }
        i++;
    }
    return i;
}

/* ==================== Object Finders (step 3c) ==============================
 * Mirrors src/hasp/hasp_object.cpp:22..69 (hasp_find_obj_from_parent_id,
 * hasp_find_obj_from_page_id, hasp_find_id_from_obj).
 *
 * LVGL 7/8 → 9 adaptations:
 *   - S3 walks children with `lv_obj_get_child(parent, prev_child)` linked-list style;
 *     LVGL 9 exposes indexed access (lv_obj_get_child_count + lv_obj_get_child(idx)).
 *   - `child->user_data.id` bit-field → hasp_obj_ud(child)->id (heap ptr, may be null).
 *   - TABVIEW branch (S3 line 37..48) skipped — tabview support is a later step.
 *   - hasp_object_tree() (debug pretty-printer) not ported yet.
 */
lv_obj_t* hasp_find_obj_from_parent_id(lv_obj_t* parent, uint8_t objid)
{
    if (objid == 0 || parent == nullptr) return parent;

    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* child = lv_obj_get_child(parent, (int32_t)i);
        if (!child) continue;

        hasp_obj_user_data_t* ud = hasp_obj_ud(child);
        if (ud && ud->id == objid) return child; /* child found */

        /* Check grandchildren (e.g., label inside a button — but its id is 0
         * so we'd only recurse for real HASP children). */
        lv_obj_t* g = hasp_find_obj_from_parent_id(child, objid);
        if (g) return g;
    }
    return nullptr;
}

lv_obj_t* hasp_find_obj_from_page_id(uint8_t pageid, uint8_t objid)
{
    return hasp_find_obj_from_parent_id(haspPages.get_obj(pageid), objid);
}

bool hasp_find_id_from_obj(const lv_obj_t* obj, uint8_t* pageid, uint8_t* objid)
{
    if (!obj || !haspPages.get_id(obj, pageid)) return false;
    hasp_obj_user_data_t* ud = hasp_obj_ud(obj);
    uint8_t id = ud ? ud->id : 0;
    /* S3 line 66: reject id==0 unless obj is the page screen itself. */
    if (id == 0 && obj != haspPages.get_obj(*pageid)) return false;
    *objid = id;
    return true;
}

/* Mirrors src/hasp/hasp_object.cpp:228 hasp_new_object().
 *
 * Flow (S3 line 262-745, preserved in 3c):
 *   1. Skip / page selection / parent resolution.
 *   2. Consume `id`, look up existing object via hasp_find_obj_from_parent_id.
 *   3. If not found → require `obj` field, switch(sdbm), create, tag user_data.
 *   4. Apply remaining JSON keys as attributes.
 *
 * Differences (documented so future me can grep):
 *   - parentid / groupid handling: groupid stored, parentid ignored (nested-parent
 *     resolution comes when we add containers with children).
 *   - LVGL 7/8 create APIs (`lv_btn_create(parent, NULL)`) → LVGL 9 (`lv_button_create(parent)`).
 *   - obj->user_data.objid = ... → allocated hasp_obj_user_data_t + lv_obj_set_user_data.
 *   - lv_obj_set_event_cb(obj, cb) → lv_obj_add_event_cb(obj, cb, LV_EVENT_ALL, NULL).
 *   - lv_obj_add_protect(PRESS_LOST) — default in LVGL 9, dropped.
 *   - lv_obj_set_gesture_parent(false) → lv_obj_remove_flag(GESTURE_BUBBLE).
 *   - lv_obj_set_click(true) — default in LVGL 9, dropped.
 *   - lv_label_set_recolor(true) — recolor removed in LVGL 9, dropped.
 */
void hasp_new_object(const JsonObject& config, uint8_t& saved_page_id)
{
    /* Skip line detection */
    if (!config[FP_SKIP].isNull() && config[FP_SKIP].as<bool>()) return;

    /* Page selection — mirrors src/hasp/hasp_object.cpp:233-247 exactly:
     * consume+remove FP_PAGE up front, only update saved_page_id after we've
     * confirmed the pageid resolves to a valid screen. */
    uint8_t pageid = saved_page_id;
    if (!config[FP_PAGE].isNull()) {
        pageid = config[FP_PAGE].as<uint8_t>();
        config.remove(FP_PAGE);
    }

    lv_obj_t* parent_obj = haspPages.get_obj(pageid);
    if (!parent_obj) {
        ESP_LOGW(TAG, "no page obj for pageid=%u", pageid);
        return;
    }
    saved_page_id = pageid;

    /* Consume id early (S3 line 263-264). */
    uint8_t id = config[FP_ID].as<uint8_t>();
    config.remove(FP_ID);

    /* 3c: lookup — S3 line 267. If object exists, skip creation and go straight
     * to attribute application (enables updates like {"id":1,"text":"..."} on
     * an existing object without re-specifying `obj`). */
    lv_obj_t* obj = hasp_find_obj_from_parent_id(parent_obj, id);
    uint16_t sdbm = 0;

    if (!obj) {
        /* Object does not exist yet — need `obj` to know what to create. */
        if (config[FP_OBJ].isNull()) {
            return; /* comment / no-op line */
        }
        sdbm = Parser::get_sdbm(config[FP_OBJ].as<const char*>());
        config.remove(FP_OBJ);

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

        hasp_obj_user_data_t* ud = (hasp_obj_user_data_t*)calloc(1, sizeof(*ud));
        if (!ud) {
            ESP_LOGE(TAG, "user_data alloc failed");
            return;
        }
        ud->id      = id;
        ud->objid   = objid;
        ud->groupid = config[FP_GROUPID].isNull() ? 0 : config[FP_GROUPID].as<uint8_t>();
        lv_obj_set_user_data(obj, ud);

        lv_obj_add_event_cb(obj, cb, LV_EVENT_ALL, nullptr);

        ESP_LOGI(TAG, "created p%ub%u objid=%u sdbm=%u", pageid, id, objid, sdbm);
    } else {
        ESP_LOGI(TAG, "reuse p%ub%u objid=%u (existing)", pageid, id, obj_get_type(obj));
        /* If caller passed `obj`, S3 warns on mismatch — 3e's hasp_attribute takes
         * care of type/attr validation. For 3c we just consume the field. */
        config.remove(FP_OBJ);
    }

    /* Apply remaining attributes (x/y/w/h/text/val in 3a scope).
     * FP_PAGE, FP_ID, FP_OBJ already removed above. Strip the rest of the
     * framework keys before iterating attributes. */
    config.remove(FP_SKIP);
    config.remove(FP_PARENTID);
    config.remove(FP_GROUPID);
    hasp_parse_json_attributes(obj, config);
}
