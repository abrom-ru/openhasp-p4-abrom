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
#include "hasp_attribute.h"   // 7E: hasp_template_task_release

#include <stdio.h>
#include <stdlib.h>
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
 *
 * 3h-4: also frees the `extra` slot (LINE points array, etc.). Widget-specific
 * per-type cleanups (my_btnmatrix_map_clear/my_msgbox_map_clear/my_image_release_
 * resources) from S3 are subsumed by the generic free(extra) — buttonmatrix map
 * and line points are plain heap arrays we own; image src is const-refed by LVGL.
 */
void delete_event_handler(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    hasp_obj_user_data_t* ud = hasp_obj_ud(obj);
    if (ud) {
        if (ud->extra) { free(ud->extra); ud->extra = nullptr; }
        /* 7E: label template attribute (strftime timer). Release before
         * user_data is freed so the timer callback never sees a dangling obj. */
        if (ud->template_task) {
            hasp_template_task_release(ud->template_task);
            ud->template_task = nullptr;
        }
        lv_obj_set_user_data(obj, nullptr);
        free(ud);
    }
}

/* Mirrors src/hasp/hasp_event.cpp:475 generic_event_handler — button/label/bar/etc.
 *
 * LVGL 9 vs S3 event stream note:
 *   S3 patched LVGL 7 to emit RELEASED before SHORT_CLICKED, and used the
 *   file-static `last_value_sent` to dedup (RELEASED after SHORT_CLICKED → drop).
 *   LVGL 9 default order is PRESSED → RELEASED → SHORT_CLICKED, so the S3 dedup
 *   would fire in the wrong direction. Additionally, LVGL 9 sends VALUE_CHANGED
 *   for clickable btn on state toggle (PRESSED bit) which S3/LVGL 7 didn't.
 *
 *   Per HASP MQTT contract a button tap is exactly {down, up}. RELEASE and
 *   CHANGED are not part of the button contract, so we drop them here.
 *   (RELEASE is still emitted by slider/toggle handlers where it's meaningful.)
 */
void generic_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { delete_event_handler(e); return; }

    /* Drop LVGL 9 button-only spam that has no HASP counterpart. */
    if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_RELEASED) return;

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
    switch (obj_get_type(obj)) {
        case LV_HASP_SLIDER:  val = lv_slider_get_value(obj);  break;
        case LV_HASP_ARC:     val = lv_arc_get_value(obj);     break;   /* 3h-4 batch 1 */
        case LV_HASP_SPINBOX: val = lv_spinbox_get_value(obj); break;   /* 3h-4 batch 2 */
        default: return;
    }

    event_object_val_event(obj, hasp_event_id, val);
}

/* ==================== 3h-4 handlers ==================== */

/* Mirrors src/hasp/hasp_event.cpp:610 selector_event_handler.
 * 3h-4 batch 1: dropdown/roller. 3h-4 batch 2: +tabview/table. */
void selector_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { delete_event_handler(e); return; }

    uint8_t hasp_event_id;
    if (!translate_event(code, hasp_event_id)) return;

    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));

    char     buffer[128] = {0};
    uint32_t val         = 0;

    switch (obj_get_type(obj)) {
        case LV_HASP_DROPDOWN:
            val = lv_dropdown_get_selected(obj);
            lv_dropdown_get_selected_str(obj, buffer, sizeof(buffer));
            break;
        case LV_HASP_ROLLER:
            val = lv_roller_get_selected(obj);
            lv_roller_get_selected_str(obj, buffer, sizeof(buffer));
            break;
        case LV_HASP_TABVIEW:
            /* Mirrors S3 selector: publish the currently active tab index.
             * LVGL 9 has no dedicated tab-name getter, so text stays empty. */
            val = lv_tabview_get_tab_active(obj);
            break;
        case LV_HASP_TABLE: {
            /* Mirrors S3 table selector: publish "row,col" via val
             * (row << 16 | col) plus the cell text. */
            uint32_t row = LV_TABLE_CELL_NONE;
            uint32_t col = LV_TABLE_CELL_NONE;
            lv_table_get_selected_cell(obj, &row, &col);
            if (row == LV_TABLE_CELL_NONE || col == LV_TABLE_CELL_NONE) return;
            val = (row << 16) | (col & 0xFFFF);
            const char* txt = lv_table_get_cell_value(obj, row, col);
            if (txt) { strncpy(buffer, txt, sizeof(buffer) - 1); buffer[sizeof(buffer) - 1] = 0; }
            break;
        }
        default:
            return;
    }

    /* Mirrors S3 event_object_selection_changed (S3 hasp_event.cpp:249) —
     * "{event, val, text}" JSON. */
    char eventname[8];
    Parser::get_event_name(hasp_event_id, eventname, sizeof(eventname));

    char data[192];
    snprintf(data, sizeof(data), "{\"event\":\"%s\",\"val\":%u,\"text\":\"%s\"}",
             eventname, (unsigned)val, buffer);
    event_send_object_data(obj, data);
}

/* Mirrors src/hasp/hasp_event.cpp:733 btnmatrix_event_handler.
 * LVGL 9 rename: lv_btnmatrix_get_active_btn/text → lv_buttonmatrix_get_selected_
 * button/button_text; LV_BTNMATRIX_BTN_NONE → LV_BUTTONMATRIX_BUTTON_NONE. */
void btnmatrix_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { delete_event_handler(e); return; }

    uint8_t hasp_event_id;
    if (!translate_event(code, hasp_event_id)) return;

    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));

    uint32_t val = lv_buttonmatrix_get_selected_button(obj);
    const char* txt = (val != LV_BUTTONMATRIX_BUTTON_NONE)
                          ? lv_buttonmatrix_get_button_text(obj, val)
                          : "";
    if (!txt) txt = "";

    char eventname[8];
    Parser::get_event_name(hasp_event_id, eventname, sizeof(eventname));

    char data[192];
    snprintf(data, sizeof(data), "{\"event\":\"%s\",\"val\":%u,\"text\":\"%s\"}",
             eventname, (unsigned)val, txt);
    event_send_object_data(obj, data);
}

/* Mirrors src/hasp/hasp_event.cpp:440 textarea_event_handler.
 * Focus/defocus hide-cursor branches are LVGL 7 only — LVGL 9 shows the cursor
 * automatically on focus and hides it on defocus, so we drop that logic. */
void textarea_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { delete_event_handler(e); return; }
    if (code != LV_EVENT_VALUE_CHANGED) return;

    lv_obj_t*   obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const char* txt = lv_textarea_get_text(obj);
    if (!txt) txt = "";

    char eventname[8];
    Parser::get_event_name(HASP_EVENT_CHANGED, eventname, sizeof(eventname));

    char data[512];
    snprintf(data, sizeof(data), "{\"event\":\"%s\",\"text\":\"%s\"}", eventname, txt);
    event_send_object_data(obj, data);
}

/* ==================== 3h-4 batch 2 handlers ==================== */

/* Mirrors src/hasp/hasp_event.cpp msgbox_event_handler.
 * LVGL 9 msgbox emits VALUE_CHANGED on the footer buttonmatrix child; the
 * matrix passes it up to the msgbox itself. We report the pressed footer
 * button index + text, plus DELETE for cleanup. */
void msgbox_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { delete_event_handler(e); return; }

    /* LVGL 9 msgbox forwards click/change from footer buttons to msgbox. */
    uint8_t hasp_event_id;
    if (!translate_event(code, hasp_event_id)) return;

    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));

    /* Best-effort: find the footer buttonmatrix child and read its active
     * button. LVGL 9 does not expose the "clicked button" via msgbox getters. */
    uint32_t    val = 0;
    const char* txt = "";
    lv_obj_t*   footer = lv_msgbox_get_footer(obj);
    if (footer) {
        uint32_t n = lv_obj_get_child_count(footer);
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t* child = lv_obj_get_child(footer, (int32_t)i);
            if (child && lv_obj_has_state(child, LV_STATE_PRESSED)) {
                val = i;
                lv_obj_t* lbl = lv_obj_get_child(child, 0);
                if (lbl) { const char* t = lv_label_get_text(lbl); if (t) txt = t; }
                break;
            }
        }
    }

    char eventname[8];
    Parser::get_event_name(hasp_event_id, eventname, sizeof(eventname));

    char data[192];
    snprintf(data, sizeof(data), "{\"event\":\"%s\",\"val\":%u,\"text\":\"%s\"}",
             eventname, (unsigned)val, txt);
    event_send_object_data(obj, data);
}

/* Mirrors src/hasp/hasp_event.cpp calendar_event_handler.
 * LVGL 9 emits VALUE_CHANGED on the internal buttonmatrix which propagates to
 * the calendar. Publish the pressed date as val=YYYYMMDD and text="YYYY-MM-DD". */
void calendar_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) { delete_event_handler(e); return; }
    if (code != LV_EVENT_VALUE_CHANGED) return;

    /* VALUE_CHANGED bubbles from the internal buttonmatrix child; use
     * current_target so we hand `lv_calendar_get_pressed_date` the calendar
     * itself, not the btnm (casting btnm → lv_calendar_t* is UB and derefs a
     * junk `calendar->btnm` pointer → load-fault in lv_buttonmatrix_get_button_text). */
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));

    lv_calendar_date_t date = {};
    if (lv_calendar_get_pressed_date(obj, &date) != LV_RESULT_OK) return;
    if (date.year == 0) return; /* extra guard: LVGL 9 can still return OK on non-day buttons */

    uint32_t val = (uint32_t)date.year * 10000u + (uint32_t)date.month * 100u + date.day;
    char text[16];
    snprintf(text, sizeof(text), "%04u-%02u-%02u",
             (unsigned)date.year, (unsigned)date.month, (unsigned)date.day);

    char eventname[8];
    Parser::get_event_name(HASP_EVENT_CHANGED, eventname, sizeof(eventname));

    char data[192];
    snprintf(data, sizeof(data), "{\"event\":\"%s\",\"val\":%u,\"text\":\"%s\"}",
             eventname, (unsigned)val, text);
    event_send_object_data(obj, data);
}
