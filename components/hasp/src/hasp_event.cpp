/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (3a-refactor): structure mirrors src/hasp/hasp_event.cpp
 * (per-type handler + LVGL event → HASP_EVENT_* translation via translate_event),
 * but each handler body is log-only. Downstream side-effects (mqtt state topic,
 * script actions, group updates) come in 3d/3e/3f. Signature is LVGL 9.
 */

#include "hasp_event.h"
#include "hasp_object.h"

#include "esp_log.h"

static const char* TAG = "hasp_evt";

/* Map LVGL 9 event code to HASP_EVENT_* — LVGL 9 adaptation of the
 * translate_event() logic embedded in S3 handlers.
 * Returns true if the event should be reported, false to swallow. */
static bool translate_event(lv_event_code_t code, uint8_t& hasp_event_id)
{
    switch (code) {
        case LV_EVENT_PRESSED:              hasp_event_id = HASP_EVENT_DOWN;    return true;
        case LV_EVENT_SHORT_CLICKED:        hasp_event_id = HASP_EVENT_UP;      return true;
        case LV_EVENT_LONG_PRESSED:         hasp_event_id = HASP_EVENT_LONG;    return true;
        case LV_EVENT_LONG_PRESSED_REPEAT:  hasp_event_id = HASP_EVENT_HOLD;    return true;
        case LV_EVENT_PRESS_LOST:           hasp_event_id = HASP_EVENT_LOST;    return true;
        case LV_EVENT_RELEASED:             hasp_event_id = HASP_EVENT_RELEASE; return true;
        case LV_EVENT_VALUE_CHANGED:        hasp_event_id = HASP_EVENT_CHANGED; return true;
        default:                            return false;
    }
}

static void log_event(const char* handler, lv_obj_t* obj, uint8_t hasp_event_id, long extra = 0)
{
    hasp_obj_user_data_t* ud = hasp_obj_ud(obj);
    uint8_t id    = ud ? ud->id    : 0;
    uint8_t objid = ud ? ud->objid : 0;
    ESP_LOGI(TAG, "%s: p_.b%u (objid=%u) evt=%u val=%ld",
             handler, id, objid, hasp_event_id, extra);
}

/**
 * Clean-up allocated memory before an object is deleted.
 * Mirrors src/hasp/hasp_event.cpp:68 delete_event_handler().
 */
void delete_event_handler(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    hasp_obj_user_data_t* ud = hasp_obj_ud(obj);
    if (ud) {
        lv_obj_set_user_data(obj, nullptr);
        free(ud);
    }
}

/* Mirrors src/hasp/hasp_event.cpp:475 generic_event_handler — button/label/bar/etc. */
void generic_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { delete_event_handler(e); return; }

    uint8_t hasp_event_id;
    if (!translate_event(code, hasp_event_id)) return;

    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    log_event("generic", obj, hasp_event_id);
}

/* Mirrors src/hasp/hasp_event.cpp:565 toggle_event_handler — switch/checkbox. */
void toggle_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { delete_event_handler(e); return; }

    uint8_t hasp_event_id;
    if (!translate_event(code, hasp_event_id)) return;

    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));

    /* Read state — LVGL 9 uses lv_obj_has_state instead of lv_switch_get_state /
     * lv_obj_get_state(&LV_BTN_PART_MAIN) & LV_STATE_CHECKED. */
    long val = lv_obj_has_state(obj, LV_STATE_CHECKED) ? 1 : 0;
    log_event("toggle", obj, hasp_event_id, val);
}

/* Mirrors src/hasp/hasp_event.cpp:802 slider_event_handler — slider/arc. */
void slider_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { delete_event_handler(e); return; }

    uint8_t hasp_event_id;
    if (!translate_event(code, hasp_event_id)) return;

    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));

    long val = 0;
    if (obj_check_type(obj, LV_HASP_SLIDER)) {
        val = lv_slider_get_value(obj);
    }
    /* LV_HASP_ARC branch will come with 3a+arc; not in scope now. */

    log_event("slider", obj, hasp_event_id, val);
}
