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
#include "hasp_dispatch.h"   // step 4b: dispatch_state_subtopic()
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

/* ==================== State dispatch (step 3f + 4b) =========================
 * Mirrors src/hasp/hasp_object.cpp:110 object_dispatch_state().
 * S3 builds the state subtopic (pageid.name or "pXbY") then hands the payload
 * to dispatch_state_subtopic → mqtt_send_state. Step 4b wires the MQTT sink
 * inside dispatch_state_subtopic; the topic-shape contract established in 3f
 * needed no changes (handlers already emit MQTT-ready JSON).
 *
 * Keeping the ESP_LOGI here at INFO makes the log stream double as a
 * pre-broker mirror during bring-up; dispatch_state_subtopic drops to DEBUG
 * when the broker isn't up (see hasp_dispatch.cpp) to avoid duplication in
 * steady state.
 */
void object_dispatch_state(uint8_t pageid, uint8_t btnid, const char* payload)
{
    if (!payload) return;

    char topic[64];
    char* pagename = haspPages.get_name(pageid);
    if (pagename)
        snprintf(topic, sizeof(topic), "%s.b%u", pagename, btnid);
    else
        snprintf(topic, sizeof(topic), "p%ub%u", pageid, btnid); /* HASP_OBJECT_NOTATION */

    ESP_LOGI(TAG, "state %s => %s", topic, payload);
    dispatch_state_subtopic(topic, payload);
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

    /* 3h-4 batch 2: parentid support — mirrors S3 hasp_object.cpp:251
     * (parentid resolution before create). Enables nested objects
     * (TAB inside TABVIEW, buttons inside CONTAINER/OBJECT, etc). */
    if (!config[FP_PARENTID].isNull()) {
        uint8_t parentid = config[FP_PARENTID].as<uint8_t>();
        if (parentid != 0) {
            lv_obj_t* np = hasp_find_obj_from_parent_id(parent_obj, parentid);
            if (np) parent_obj = np;
            else    ESP_LOGW(TAG, "parentid %u not found on page %u", parentid, pageid);
        }
        /* FP_PARENTID removed at end of hasp_new_object with the rest of
         * framework keys — leave here so attribute processor can ignore it. */
    }

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

            /* ----- 3h-4 batch 1: arc / led / image / line / dropdown /
             *                     roller / spinner / btnmatrix / textarea ---- */

            /* ARC — S3 hasp_object.cpp:391. Uses slider_event_handler
             * (extended in 3h-4 with LV_HASP_ARC branch). */
            case LV_HASP_ARC:
            case HASP_OBJ_ARC: {
                obj = lv_arc_create(parent_obj);
                if (obj) {
                    cb    = slider_event_handler;
                    objid = LV_HASP_ARC;
                }
                break;
            }

            /* LED — S3 hasp_object.cpp:438. Reports touch events via generic. */
            case LV_HASP_LED:
            case HASP_OBJ_LED: {
                obj = lv_led_create(parent_obj);
                if (obj) {
                    cb    = generic_event_handler;
                    objid = LV_HASP_LED;
                }
                break;
            }

            /* IMAGE — S3 hasp_object.cpp:371 (lv_img_create → lv_image_create). */
            case LV_HASP_IMAGE:
            case HASP_OBJ_IMG: {
                obj = lv_image_create(parent_obj);
                if (obj) {
                    cb    = generic_event_handler;
                    objid = LV_HASP_IMAGE;
                }
                break;
            }

            /* LINE — S3 hasp_object.cpp:553. Delete-hook (extra slot) frees
             * the points buffer allocated by ATTR_POINTS. */
            case LV_HASP_LINE:
            case HASP_OBJ_LINE: {
                obj = lv_line_create(parent_obj);
                if (obj) {
                    /* S3 sets line_width=1 via LVGL 7 local style. Do the same
                     * in LVGL 9 (default is 0 → invisible). */
                    lv_obj_set_style_line_width(obj, 1, LV_PART_MAIN);
                    cb    = generic_event_handler;
                    objid = LV_HASP_LINE;
                }
                break;
            }

            /* DROPDOWN — S3 hasp_object.cpp:639. */
            case LV_HASP_DROPDOWN:
            case HASP_OBJ_DROPDOWN: {
                obj = lv_dropdown_create(parent_obj);
                if (obj) {
                    lv_dropdown_set_text(obj, NULL); /* clear default "..." */
                    /* LVGL 9 has no lv_dropdown_set_draw_arrow — arrow is set
                     * via lv_dropdown_set_symbol (defaults to LV_SYMBOL_DOWN). */
                    cb    = selector_event_handler;
                    objid = LV_HASP_DROPDOWN;
                }
                break;
            }

            /* ROLLER — S3 hasp_object.cpp:653. LVGL 9 dropped set_auto_fit —
             * roller sizes to `visible_row_count` explicitly. */
            case LV_HASP_ROLLER:
            case HASP_OBJ_ROLLER: {
                obj = lv_roller_create(parent_obj);
                if (obj) {
                    cb    = selector_event_handler;
                    objid = LV_HASP_ROLLER;
                }
                break;
            }

            /* SPINNER — S3 hasp_object.cpp:520. LVGL 9 signature is
             * lv_spinner_create(parent) then set_anim_params(t, angle). */
            case LV_HASP_SPINNER:
            case HASP_OBJ_SPINNER: {
                obj = lv_spinner_create(parent_obj);
                if (obj) {
                    lv_spinner_set_anim_params(obj, 1000, 60);
                    cb    = generic_event_handler;
                    objid = LV_HASP_SPINNER;
                }
                break;
            }

            /* BTNMATRIX — S3 hasp_object.cpp:296. LVGL 9 rename:
             * lv_btnmatrix_* → lv_buttonmatrix_*. `recolor` removed in v8+
             * (text renders literal '#rrggbb ' tokens now → theme handles it). */
            case LV_HASP_BTNMATRIX:
            case HASP_OBJ_BTNMATRIX: {
                obj = lv_buttonmatrix_create(parent_obj);
                if (obj) {
                    cb    = btnmatrix_event_handler;
                    objid = LV_HASP_BTNMATRIX;
                }
                break;
            }

            /* TEXTAREA — S3 hasp_object.cpp:361. */
            case LV_HASP_TEXTAREA:
            case HASP_OBJ_TEXTAREA: {
                obj = lv_textarea_create(parent_obj);
                if (obj) {
                    lv_textarea_set_cursor_click_pos(obj, true);
                    cb    = textarea_event_handler;
                    objid = LV_HASP_TEXTAREA;
                }
                break;
            }

            /* ==================== 3h-4 batch 2 ==================== */

            /* Generic container — S3 line 289 (ALARM), 400 (CONTAINER), 409 (OBJECT).
             * All three sdbm aliases collapse to bare lv_obj_create in LVGL 9. */
            case LV_HASP_OBJECT:
            case LV_HASP_CONTAINER:
            case LV_HASP_ALARM:
            case HASP_OBJ_OBJ:
            case HASP_OBJ_CONT:
            case HASP_OBJ_ALARM: {
                obj = lv_obj_create(parent_obj);
                if (obj) {
                    cb    = generic_event_handler;
                    /* Preserve the caller's intent so obj_get_type sees the
                     * exact alias they used (LV_HASP_OBJECT vs CONTAINER vs ALARM).
                     * Match both the small enum values (LV_HASP_*) and the
                     * sdbm hashes (HASP_OBJ_*) — jsonl "obj":"alarm" produces
                     * the hash, whereas obsolete "objid":60 produces the enum. */
                    switch (sdbm) {
                        case LV_HASP_ALARM:
                        case HASP_OBJ_ALARM:     objid = LV_HASP_ALARM;     break;
                        case LV_HASP_CONTAINER:
                        case HASP_OBJ_CONT:      objid = LV_HASP_CONTAINER; break;
                        default:                 objid = LV_HASP_OBJECT;    break;
                    }
                }
                break;
            }

            /* TABLE — S3 hasp_object.cpp:313. */
            case LV_HASP_TABLE:
            case HASP_OBJ_TABLE: {
                obj = lv_table_create(parent_obj);
                if (obj) {
                    cb    = selector_event_handler;
                    objid = LV_HASP_TABLE;
                }
                break;
            }

            /* QRCODE — S3 hasp_object.cpp:381. LVGL 9 signature dropped the
             * initial size+colors — call setters afterwards. Requires
             * CONFIG_LV_USE_QRCODE=y (set in esp/sdkconfig.defaults). */
            case LV_HASP_QRCODE:
            case HASP_OBJ_QRCODE: {
                obj = lv_qrcode_create(parent_obj);
                if (obj) {
                    lv_qrcode_set_size(obj, 140);
                    lv_qrcode_set_dark_color(obj, lv_color_black());
                    lv_qrcode_set_light_color(obj, lv_color_white());
                    cb    = generic_event_handler;
                    objid = LV_HASP_QRCODE;
                }
                break;
            }

            /* SPINBOX — S3 hasp_object.cpp:585. Uses slider_event_handler
             * (extended above with LV_HASP_SPINBOX branch). */
            case LV_HASP_SPINBOX:
            case HASP_OBJ_SPINBOX: {
                obj = lv_spinbox_create(parent_obj);
                if (obj) {
                    lv_spinbox_set_range(obj, 0, 100);
                    cb    = slider_event_handler;
                    objid = LV_HASP_SPINBOX;
                }
                break;
            }

            /* MSGBOX — S3 hasp_object.cpp:664. LVGL 9 API is very different:
             * create(NULL) makes a modal msgbox on the active screen root,
             * create(parent) makes an in-place one. Title/text/buttons are
             * attached via add_title/add_text/add_footer_button.
             * We create in-place (parent_obj) — modal covers the whole screen
             * which is rarely what an HASP jsonl wants. */
            case LV_HASP_MSGBOX:
            case HASP_OBJ_MSGBOX: {
                obj = lv_msgbox_create(parent_obj);
                if (obj) {
                    cb    = msgbox_event_handler;
                    objid = LV_HASP_MSGBOX;
                }
                break;
            }

            /* TABVIEW — S3 hasp_object.cpp:454. LVGL 9 signature is
             * lv_tabview_create(parent), then set_tab_bar_position/size. */
            case LV_HASP_TABVIEW:
            case HASP_OBJ_TABVIEW: {
                obj = lv_tabview_create(parent_obj);
                if (obj) {
                    lv_tabview_set_tab_bar_position(obj, LV_DIR_TOP);
                    lv_tabview_set_tab_bar_size(obj, 50);
                    cb    = selector_event_handler;
                    objid = LV_HASP_TABVIEW;
                }
                break;
            }

            /* TAB — S3 hasp_object.cpp:494. Must have a TABVIEW parent. */
            case LV_HASP_TAB:
            case HASP_OBJ_TAB: {
                if (parent_obj && obj_check_type(parent_obj, LV_HASP_TABVIEW)) {
                    obj = lv_tabview_add_tab(parent_obj, "Tab");
                    if (obj) {
                        cb    = generic_event_handler;
                        objid = LV_HASP_TAB;
                    }
                } else {
                    ESP_LOGW(TAG, "parent of TAB must be a TABVIEW");
                    return;
                }
                break;
            }

            /* TILEVIEW — S3 hasp_object.cpp:475. No dedicated handler. */
            case LV_HASP_TILEVIEW:
            case HASP_OBJ_TILEVIEW: {
                obj = lv_tileview_create(parent_obj);
                if (obj) {
                    cb    = generic_event_handler;
                    objid = LV_HASP_TILEVIEW;
                }
                break;
            }

            /* LIST — S3 hasp_object.cpp:596. Extra widget; event callbacks land
             * on individual list buttons, not on the container. */
            case LV_HASP_LIST:
            case HASP_OBJ_LIST: {
                obj = lv_list_create(parent_obj);
                if (obj) {
                    cb    = generic_event_handler;
                    objid = LV_HASP_LIST;
                }
                break;
            }

            /* CALENDAR — S3 hasp_object.cpp:682. Two enum aliases: the S3
             * legacy misspelling LV_HASP_CALENDER (with an 'e') and the sdbm
             * HASP_OBJ_CALENDAR (correct spelling). Keep both. */
            case LV_HASP_CALENDER:
            case HASP_OBJ_CALENDAR: {
                obj = lv_calendar_create(parent_obj);
                if (obj) {
                    cb    = calendar_event_handler;
                    objid = LV_HASP_CALENDER;
                }
                break;
            }

            /* CHART — S3 hasp_object.cpp:606. LVGL 9 rename:
             * lv_chart_set_range → lv_chart_set_axis_range (per-axis),
             * lv_chart_set_next  → lv_chart_set_next_value,
             * lv_chart_add_series takes an axis argument. We create with a
             * single primary-Y series so an empty jsonl entry still shows
             * something; more series can be added later via attributes. */
            case LV_HASP_CHART:
            case HASP_OBJ_CHART: {
                obj = lv_chart_create(parent_obj);
                if (obj) {
                    lv_chart_set_axis_range(obj, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
                    lv_chart_add_series(obj, lv_color_hex(0x00b7ff), LV_CHART_AXIS_PRIMARY_Y);
                    cb    = generic_event_handler;
                    objid = LV_HASP_CHART;
                }
                break;
            }

            default:
                ESP_LOGW(TAG, "unsupported obj sdbm=%u (3h-4 batch 2 covers 25+ widgets — see hasp_object.cpp)", sdbm);
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
