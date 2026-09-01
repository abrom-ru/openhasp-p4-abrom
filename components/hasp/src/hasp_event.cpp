/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (3a → 3f). Structure mirrors src/hasp/hasp_event.cpp
 * (per-type handler + LVGL event → HASP_EVENT_* translation via translate_event),
 * signature is LVGL 9 (single lv_event_t*).
 *
 * 3f changes vs 3a log-only skeleton:
 *   - Handlers now build the same JSON payloads as S3 event_object_val_event
 *     (S3 hasp_event.cpp:281) and forward via object_dispatch_state()
 *     (defined in hasp_object.cpp, log-only sink for now — MQTT lands in step 4).
 *   - Event-name is resolved through Parser::get_event_name() (ported in 3f).
 *
 * Deferred vs S3 (documented so future me can grep):
 *   - Value-dedup slot (`last_value_sent`/`last_obj_sent`) → not ported. LVGL 9
 *     already coalesces the pointer-VALUE_CHANGED storm for slider drag; if
 *     dedup becomes necessary re-add the two file-static slots + reset hook.
 *   - script_event_handler / action / swipe → user_data doesn't yet store
 *     tag/action/swipe (comes with hasp_attribute_helper.h port in 3g).
 *   - dispatch_normalized_group_values → group broadcast; groups are stored in
 *     hasp_obj_user_data_t.groupid but the fan-out dispatcher isn't ported yet.
 *   - first_touch_event_handler / hasp_update_sleep_state → needs antiburn +
 *     backlight modules from src/hasp/hasp.cpp (step 5).
 *   - selector/btnmatrix/msgbox/cpicker/calendar handlers → widgets not created
 *     yet (dropdown/roller/tabview/table/cpicker land with 3g styles+widgets).
 */

#include "hasp_event.h"
#include "hasp_object.h"
#include "hasp_parser.h"

#include <stdio.h>
#include <string.h>

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

/* Mirrors S3 hasp_event.cpp:267 event_send_object_data().
 * Resolves (pageid, objid) via hasp_find_id_from_obj and forwards to
 * object_dispatch_state — the same seam MQTT will hook in step 4. */
static void event_send_object_data(lv_obj_t* obj, const char* data)
{
    uint8_t pageid;
    uint8_t objid;
    if (hasp_find_id_from_obj(obj, &pageid, &objid)) {
        object_dispatch_state(pageid, objid, data);
    } else {
        ESP_LOGW(TAG, "event: object not registered (skip publish)");
    }
}

/* Mirrors S3 hasp_event.cpp:281 event_object_val_event(). */
static void event_object_val_event(lv_obj_t* obj, uint8_t hasp_event_id, long val)
{
    char eventname[8];
    Parser::get_event_name(hasp_event_id, eventname, sizeof(eventname));

    char data[128];
    snprintf(data, sizeof(data), "{\"event\":\"%s\",\"val\":%ld}", eventname, val);
    event_send_object_data(obj, data);
}

/* Mirrors S3 hasp_event.cpp:541 generic path (no-val variant). */
static void event_object_evt_event(lv_obj_t* obj, uint8_t hasp_event_id)
{
    char eventname[8];
    Parser::get_event_name(hasp_event_id, eventname, sizeof(eventname));

    char data[64];
    snprintf(data, sizeof(data), "{\"event\":\"%s\"}", eventname);
    event_send_object_data(obj, data);
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
    event_object_evt_event(obj, hasp_event_id);
}

/* Mirrors src/hasp/hasp_event.cpp:565 toggle_event_handler — switch/checkbox. */
void toggle_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { delete_event_handler(e); return; }

    uint8_t hasp_event_id;
    if (!translate_event(code, hasp_event_id)) return;
    /* S3 line 572: only up/down carry the toggle state for switch/checkbox. */
    if (hasp_event_id != HASP_EVENT_DOWN && hasp_event_id != HASP_EVENT_UP) return;

    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));

    /* Read state — LVGL 9 uses lv_obj_has_state instead of lv_switch_get_state /
     * lv_obj_get_state(&LV_BTN_PART_MAIN) & LV_STATE_CHECKED. */
    long val = lv_obj_has_state(obj, LV_STATE_CHECKED) ? 1 : 0;
    event_object_val_event(obj, hasp_event_id, val);
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
    /* LV_HASP_ARC branch will come with 3g; not in scope now. */

    event_object_val_event(obj, hasp_event_id, val);
}
