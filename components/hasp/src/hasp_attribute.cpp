/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (step 3e) from src/hasp/hasp_attribute.cpp (2960 lines
 * in S3). This is the MVP slice — enough to make dispatch commands actually
 * mutate live widgets in the ways users care about most:
 *   geometry  : x/y/w/h/size/ext_click_h/v
 *   visibility: hidden/vis/click/enabled
 *   state     : toggle, val, min, max
 *   text      : text/template, mode (long_mode), align (text_align)
 *   style     : bg_color, text_color, opacity (opa) — the three colors people
 *               set 99% of the time.
 *   methods   : delete, clear, to_front, to_back, comment (nop)
 *
 * Structure mirrors S3 hasp_process_obj_attribute (S3:2657) — same top-level
 * switch(attr_hash), same helper decomposition (attribute_common_* /
 * attribute_local_style_*), same return-code semantics (hasp_attribute_type_t).
 * Cases we haven't ported yet fall through to HASP_ATTR_TYPE_NOT_FOUND and log
 * a warning — matches S3 behaviour and makes remaining gaps grep-able.
 *
 * NOT ported (deferred — grep TODO_3f/3g for callers):
 *   - specific_int/coord/bool/page_attribute (per-widget properties beyond val/min/max)
 *   - special_attribute_direction, special_attribute_src (image/dropdown)
 *   - attribute_common_tag/json (needs user_data extension — 3f/4)
 *   - attribute_common_json (jsonl-as-attr — recursion through hasp_new_object)
 *   - hasp_local_style_attr full property table (padding/border/shadow/outline/…)
 *   - attr_out_* dispatch (query path publishes nothing yet — 3f wires MQTT)
 *   - hasp_process_arc/slider/spinner/gauge/lmeter_attribute specific fallthrough
 *   - haspPages.set_name / ATTR_NAME (needs Page.name field — later)
 *
 * LVGL 7 → 9 mapping cheat-sheet (documented once, applied throughout):
 *   set_hidden(x,b)                → add/remove_flag(x, LV_OBJ_FLAG_HIDDEN)
 *   get_hidden(x)                  → has_flag(x, LV_OBJ_FLAG_HIDDEN)
 *   set_click(x,b)                 → add/remove_flag(x, LV_OBJ_FLAG_CLICKABLE)
 *   btn_set/get_checkable          → add/remove/has_flag(LV_OBJ_FLAG_CHECKABLE)
 *   switch_on/off(x, anim)         → add/remove_state(x, LV_STATE_CHECKED)
 *   switch_get_state(x)            → has_state(x, LV_STATE_CHECKED)
 *   checkbox_set/is_checked        → add/remove/has_state(LV_STATE_CHECKED)
 *   set_style_local_opa_scale      → set_style_opa(x, val, LV_PART_MAIN)
 *   get_style_opa_scale            → get_style_opa(x, LV_PART_MAIN)
 *   set_style_local_bg_color       → set_style_bg_color(x, c, LV_PART_MAIN)
 *   set_style_local_text_color     → set_style_text_color(x, c, LV_PART_MAIN)
 *   label_set/get_align            → set/get_style_text_align(x, a, LV_PART_MAIN)
 *   ext_click_pad_left/right/top/bottom → single lv_obj_set_ext_click_area(x, size)
 */

#include "hasp_attribute.h"
#include "hasp_object.h"
#include "hasp_parser.h"

#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>

#include "esp_log.h"

static const char* TAG = "hasp_attr";

/* ---------- small helpers ---------- */

/* LVGL 9 has no per-side ext_click_area getter — best we can do for get path
 * is remember what we set. hasp_object stores nothing yet; MVP scope is
 * "set only, get returns 0". Matches S3 semantics for anything the theme
 * hasn't already set. */
static void obj_set_ext_click_all(lv_obj_t* obj, int32_t pad)
{
    lv_obj_set_ext_click_area(obj, pad);
}

/* Locate the label child of a HASP button (created in hasp_object.cpp button
 * branch — first child, tagged LV_HASP_LABEL). Returns nullptr for non-buttons
 * or buttons that lost their child (shouldn't happen in normal flow). */
static lv_obj_t* find_button_label(lv_obj_t* btn)
{
    if (!btn || obj_get_type(btn) != LV_HASP_BUTTON) return nullptr;
    return lv_obj_get_child(btn, 0);
}

/* ==================== common: integer geometry / opacity (S3:2373) ==================== */

static hasp_attribute_type_t attribute_common_int(lv_obj_t* obj, uint16_t attr_hash, int32_t& val, bool update)
{
    hasp_obj_user_data_t* ud = hasp_obj_ud(obj);

    switch (attr_hash) {
        case ATTR_ID:
            if (ud) { if (update) ud->id = (uint8_t)val; else val = ud->id; }
            break;
        case ATTR_GROUPID:
            if (ud) { if (update) ud->groupid = (uint8_t)val; else val = ud->groupid; }
            break;
        case ATTR_OBJID:
            if (!ud) return HASP_ATTR_TYPE_NOT_FOUND;
            if (update && val != ud->objid) return HASP_ATTR_TYPE_INT_READONLY;
            val = ud->objid;
            break;
        case ATTR_X:
            if (update) lv_obj_set_x(obj, val); else val = lv_obj_get_x(obj);
            break;
        case ATTR_Y:
            if (update) lv_obj_set_y(obj, val); else val = lv_obj_get_y(obj);
            break;
        case ATTR_W:
            if (update) lv_obj_set_width(obj, val); else val = lv_obj_get_width(obj);
            break;
        case ATTR_H:
            if (update) lv_obj_set_height(obj, val); else val = lv_obj_get_height(obj);
            break;
        case ATTR_OPACITY:
            if (update) lv_obj_set_style_opa(obj, (lv_opa_t)val, LV_PART_MAIN);
            else        val = lv_obj_get_style_opa(obj, LV_PART_MAIN);
            break;
        case ATTR_EXT_CLICK_H:
        case ATTR_EXT_CLICK_V:
            /* LVGL 9 dropped the per-axis API — we set both axes to the same pad. */
            if (update) obj_set_ext_click_all(obj, val); else val = 0;
            break;
        default:
            return HASP_ATTR_TYPE_NOT_FOUND;
    }
    return HASP_ATTR_TYPE_INT;
}

/* ==================== common: boolean state (S3:2494) ==================== */

static hasp_attribute_type_t attribute_common_bool(lv_obj_t* obj, uint16_t attr_hash, int32_t& val, bool update)
{
    switch (attr_hash) {
        case ATTR_VIS:
            if (update) { if (val) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
                          else     lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); }
            else val = !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
            break;
        case ATTR_HIDDEN:
            if (update) { if (val) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
                          else     lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN); }
            else val = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
            break;
        case ATTR_CLICK:
            if (update) { if (val) lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
                          else     lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE); }
            else val = lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            break;
        case ATTR_ENABLED:
            if (update) { if (val) lv_obj_remove_state(obj, LV_STATE_DISABLED);
                          else     lv_obj_add_state(obj, LV_STATE_DISABLED); }
            else val = !(lv_obj_get_state(obj) & LV_STATE_DISABLED);
            break;
        case ATTR_TOGGLE:
            switch (obj_get_type(obj)) {
                case LV_HASP_BUTTON:
                    if (update) {
                        if (val) lv_obj_add_flag(obj, LV_OBJ_FLAG_CHECKABLE);
                        else     lv_obj_remove_flag(obj, LV_OBJ_FLAG_CHECKABLE);
                    } else {
                        val = lv_obj_has_flag(obj, LV_OBJ_FLAG_CHECKABLE);
                    }
                    break;
                default:
                    return HASP_ATTR_TYPE_NOT_FOUND;
            }
            break;
        default:
            return HASP_ATTR_TYPE_NOT_FOUND;
    }
    return HASP_ATTR_TYPE_BOOL;
}

/* ==================== common: val (S3:2070) ==================== */

static hasp_attribute_type_t attribute_common_val(lv_obj_t* obj, int32_t& val, bool update)
{
    switch (obj_get_type(obj)) {
        case LV_HASP_BUTTON:
            if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CHECKABLE)) {
                if (update) { if (val) lv_obj_add_state(obj, LV_STATE_CHECKED);
                              else     lv_obj_remove_state(obj, LV_STATE_CHECKED); }
                else val = (lv_obj_get_state(obj) & LV_STATE_CHECKED) ? 1 : 0;
            } else {
                return HASP_ATTR_TYPE_NOT_FOUND;
            }
            break;
        case LV_HASP_CHECKBOX:
            if (update) { if (val) lv_obj_add_state(obj, LV_STATE_CHECKED);
                          else     lv_obj_remove_state(obj, LV_STATE_CHECKED); }
            else val = lv_obj_has_state(obj, LV_STATE_CHECKED);
            break;
        case LV_HASP_SWITCH:
            if (update) { if (val) lv_obj_add_state(obj, LV_STATE_CHECKED);
                          else     lv_obj_remove_state(obj, LV_STATE_CHECKED); }
            else val = lv_obj_has_state(obj, LV_STATE_CHECKED);
            break;
        case LV_HASP_SLIDER:
            if (update) lv_slider_set_value(obj, val, LV_ANIM_ON);
            else        val = lv_slider_get_value(obj);
            break;
        case LV_HASP_BAR:
            if (update) lv_bar_set_value(obj, val, LV_ANIM_ON);
            else        val = lv_bar_get_value(obj);
            break;
        /* ARC / DROPDOWN / ROLLER / GAUGE / SPINBOX / TABVIEW — TODO_3g: wire when
         * those widgets land in hasp_object create switch. */
        default:
            return HASP_ATTR_TYPE_NOT_FOUND;
    }
    return HASP_ATTR_TYPE_INT;
}

/* ==================== common: range (S3:2233) ==================== */

static bool obj_get_range_mvp(lv_obj_t* obj, int32_t& min, int32_t& max)
{
    switch (obj_get_type(obj)) {
        case LV_HASP_SLIDER: min = lv_slider_get_min_value(obj); max = lv_slider_get_max_value(obj); return true;
        case LV_HASP_BAR:    min = lv_bar_get_min_value(obj);    max = lv_bar_get_max_value(obj);    return true;
        default: return false;
    }
}

static hasp_attribute_type_t attribute_common_range(lv_obj_t* obj, int32_t& val, bool update,
                                                    bool set_min, bool set_max)
{
    int32_t min, max;
    if (!obj_get_range_mvp(obj, min, max)) return HASP_ATTR_TYPE_RANGE_ERROR;

    switch (obj_get_type(obj)) {
        case LV_HASP_SLIDER:
            if (update && (set_min ? val : min) == (set_max ? val : max)) return HASP_ATTR_TYPE_RANGE_ERROR;
            if (update) lv_slider_set_range(obj, set_min ? val : min, set_max ? val : max);
            else        val = set_min ? min : max;
            break;
        case LV_HASP_BAR:
            if (update && (set_min ? val : min) == (set_max ? val : max)) return HASP_ATTR_TYPE_RANGE_ERROR;
            if (update) lv_bar_set_range(obj, set_min ? val : min, set_max ? val : max);
            else        val = set_min ? min : max;
            break;
        default:
            return HASP_ATTR_TYPE_NOT_FOUND;
    }
    return HASP_ATTR_TYPE_INT;
}

/* ==================== common: text (S3:1753) ==================== */

static hasp_attribute_type_t attribute_common_text(lv_obj_t* obj, uint16_t attr_hash,
                                                   const char* payload, bool update)
{
    /* S3 has a big const table dispatching on {obj_type, attr_hash}. We inline
     * the same table as a switch — smaller in code and easier to read for the
     * types we currently support. */
    switch (obj_get_type(obj)) {
        case LV_HASP_LABEL:
            if (attr_hash == ATTR_TEXT || attr_hash == ATTR_TXT || attr_hash == ATTR_TEMPLATE) {
                if (update) lv_label_set_text(obj, payload);
                return HASP_ATTR_TYPE_STR;
            }
            break;
        case LV_HASP_CHECKBOX:
            if (attr_hash == ATTR_TEXT || attr_hash == ATTR_TXT) {
                if (update) lv_checkbox_set_text(obj, payload);
                return HASP_ATTR_TYPE_STR;
            }
            break;
        case LV_HASP_BUTTON:
            if (attr_hash == ATTR_TEXT || attr_hash == ATTR_TXT) {
                lv_obj_t* lbl = find_button_label(obj);
                if (!lbl) return HASP_ATTR_TYPE_NOT_FOUND;
                if (update) lv_label_set_text(lbl, payload);
                return HASP_ATTR_TYPE_STR;
            }
            break;
        default:
            break;
    }
    return HASP_ATTR_TYPE_NOT_FOUND;
}

/* ==================== common: align (S3:1534) ==================== */

static hasp_attribute_type_t attribute_common_align(lv_obj_t* obj, const char* payload, bool update)
{
    lv_text_align_t align = LV_TEXT_ALIGN_LEFT;

    if (update) {
        if      (!strcasecmp(payload, "left"))   align = LV_TEXT_ALIGN_LEFT;
        else if (!strcasecmp(payload, "right"))  align = LV_TEXT_ALIGN_RIGHT;
        else if (!strcasecmp(payload, "center")) align = LV_TEXT_ALIGN_CENTER;
        else if (!strcasecmp(payload, "auto"))   align = LV_TEXT_ALIGN_AUTO;
        else {
            int v = atoi(payload);
            if (v < 0 || v > LV_TEXT_ALIGN_AUTO) return HASP_ATTR_TYPE_ALIGN_INVALID;
            align = (lv_text_align_t)v;
        }
    }

    lv_obj_t* target = obj;
    if (obj_get_type(obj) == LV_HASP_BUTTON) {
        target = find_button_label(obj);
        if (!target) return HASP_ATTR_TYPE_NOT_FOUND;
    }

    switch (obj_get_type(obj)) {
        case LV_HASP_BUTTON:
        case LV_HASP_LABEL:
            if (update) lv_obj_set_style_text_align(target, align, LV_PART_MAIN);
            break;
        default:
            return HASP_ATTR_TYPE_NOT_FOUND;
    }
    return HASP_ATTR_TYPE_ALIGN;
}

/* ==================== common: mode (label long_mode, S3:1621) ==================== */

static hasp_attribute_type_t attribute_common_mode(lv_obj_t* obj, const char* payload, bool update)
{
    lv_obj_t* target = obj;
    if (obj_get_type(obj) == LV_HASP_BUTTON) {
        target = find_button_label(obj);
        if (!target) return HASP_ATTR_TYPE_NOT_FOUND;
    } else if (obj_get_type(obj) != LV_HASP_LABEL) {
        return HASP_ATTR_TYPE_NOT_FOUND;
    }

    if (!update) return HASP_ATTR_TYPE_STR; /* get: 3f will publish; nothing to do */

    lv_label_long_mode_t mode;
    if      (!strcasecmp(payload, "wrap"))            mode = LV_LABEL_LONG_MODE_WRAP;
    else if (!strcasecmp(payload, "dots"))            mode = LV_LABEL_LONG_MODE_DOTS;
    else if (!strcasecmp(payload, "scroll"))          mode = LV_LABEL_LONG_MODE_SCROLL;
    else if (!strcasecmp(payload, "loop"))            mode = LV_LABEL_LONG_MODE_SCROLL_CIRCULAR;
    else if (!strcasecmp(payload, "crop") ||
             !strcasecmp(payload, "clip"))            mode = LV_LABEL_LONG_MODE_CLIP;
    else                                              return HASP_ATTR_TYPE_LONG_MODE_INVALID;

    lv_label_set_long_mode(target, mode);
    return HASP_ATTR_TYPE_STR;
}

/* ==================== common: methods (S3:2325) ==================== */

static hasp_attribute_type_t attribute_common_method(lv_obj_t* obj, uint16_t attr_hash)
{
    switch (attr_hash) {
        case ATTR_DELETE:
            if (!lv_obj_get_parent(obj)) return HASP_ATTR_TYPE_METHOD_INVALID_FOR_PAGE;
            lv_obj_delete_async(obj);
            break;
        case ATTR_CLEAR:
            lv_obj_clean(obj);
            break;
        case ATTR_TO_FRONT: {
            /* LVGL 9 replacement of lv_obj_move_foreground(): move to last
             * index within the parent (last child == topmost). */
            lv_obj_t* parent = lv_obj_get_parent(obj);
            if (!parent) return HASP_ATTR_TYPE_METHOD_INVALID_FOR_PAGE;
            uint32_t cnt = lv_obj_get_child_count(parent);
            if (cnt > 0) lv_obj_move_to_index(obj, (int32_t)cnt - 1);
            break;
        }
        case ATTR_TO_BACK:
            /* LVGL 9 replacement of lv_obj_move_background(). */
            if (!lv_obj_get_parent(obj)) return HASP_ATTR_TYPE_METHOD_INVALID_FOR_PAGE;
            lv_obj_move_to_index(obj, 0);
            break;
        default:
            return HASP_ATTR_TYPE_NOT_FOUND;
    }
    return HASP_ATTR_TYPE_METHOD_OK;
}

/* ==================== local style: colors (S3:721 subset) ==================== */

static hasp_attribute_type_t attribute_local_style(lv_obj_t* obj, uint16_t attr_hash,
                                                   const char* payload, bool update)
{
    /* MVP: the two colors people set 99% of the time. Everything else falls
     * through to NOT_FOUND — will be filled in 3g when we port the full
     * property table. */
    switch (attr_hash) {
        case ATTR_BG_COLOR: {
            if (update) {
                lv_color32_t c;
                if (!Parser::haspPayloadToColor(payload, c)) return HASP_ATTR_TYPE_COLOR_INVALID;
                lv_obj_set_style_bg_color(obj, lv_color_make(c.red, c.green, c.blue), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
            }
            return HASP_ATTR_TYPE_COLOR;
        }
        case ATTR_TEXT_COLOR: {
            if (update) {
                lv_color32_t c;
                if (!Parser::haspPayloadToColor(payload, c)) return HASP_ATTR_TYPE_COLOR_INVALID;
                lv_obj_set_style_text_color(obj, lv_color_make(c.red, c.green, c.blue), LV_PART_MAIN);
            }
            return HASP_ATTR_TYPE_COLOR;
        }
        default:
            return HASP_ATTR_TYPE_NOT_FOUND;
    }
}

/* ==================== dispatcher — mirrors S3:2657 ==================== */

void hasp_process_obj_attribute(lv_obj_t* obj, const char* attribute, const char* payload, bool update)
{
    if (!obj || !attribute || !payload) return;

    int32_t val = 0;
    hasp_attribute_type_t ret = HASP_ATTR_TYPE_NOT_FOUND;
    uint16_t attr_hash = Parser::get_sdbm(attribute);

    switch (attr_hash) {

        case ATTR_GROUPID:
        case ATTR_ID:
        case ATTR_OBJID:
        case ATTR_X:
        case ATTR_Y:
        case ATTR_H:
        case ATTR_W:
        case ATTR_OPACITY:
        case ATTR_EXT_CLICK_H:
        case ATTR_EXT_CLICK_V:
            val = strtol(payload, nullptr, 10);
            ret = attribute_common_int(obj, attr_hash, val, update);
            break;

        case ATTR_VIS:
        case ATTR_HIDDEN:
        case ATTR_TOGGLE:
        case ATTR_CLICK:
        case ATTR_ENABLED:
            val = Parser::is_true(payload);
            ret = attribute_common_bool(obj, attr_hash, val, update);
            break;

        case ATTR_MIN:
            val = strtol(payload, nullptr, 10);
            ret = attribute_common_range(obj, val, update, true, false);
            break;
        case ATTR_MAX:
            val = strtol(payload, nullptr, 10);
            ret = attribute_common_range(obj, val, update, false, true);
            break;

        case ATTR_VAL:
            val = strtol(payload, nullptr, 10);
            ret = attribute_common_val(obj, val, update);
            break;

        case ATTR_TXT:
        case ATTR_TEXT:
        case ATTR_TEMPLATE:
            ret = attribute_common_text(obj, attr_hash, payload, update);
            break;

        case ATTR_ALIGN:
            ret = attribute_common_align(obj, payload, update);
            break;

        case ATTR_MODE:
            ret = attribute_common_mode(obj, payload, update);
            break;

        case ATTR_DELETE:
        case ATTR_CLEAR:
        case ATTR_TO_FRONT:
        case ATTR_TO_BACK:
            ret = attribute_common_method(obj, attr_hash);
            break;

        case ATTR_COMMENT:
            ret = HASP_ATTR_TYPE_METHOD_OK;
            break;

        default:
            /* Fall through to local-style / per-widget attributes. */
            ret = attribute_local_style(obj, attr_hash, payload, update);
            break;
    }

    /* Report status. Real MQTT publish for the query path comes in 3f — for
     * now we log values so smoke tests can see get semantics work. */
    if (update && ret > 0) return; /* success on write */

    switch (ret) {
        case HASP_ATTR_TYPE_NOT_FOUND:
            ESP_LOGW(TAG, "unknown attribute '%s' (hash=%u)", attribute, attr_hash);
            break;
        case HASP_ATTR_TYPE_INT_READONLY:
            ESP_LOGW(TAG, "attribute '%s' is read-only", attribute);
            break;
        case HASP_ATTR_TYPE_INT:
        case HASP_ATTR_TYPE_ALIGN:
            ESP_LOGI(TAG, "get %s=%ld", attribute, (long)val);
            break;
        case HASP_ATTR_TYPE_BOOL:
            ESP_LOGI(TAG, "get %s=%s", attribute, val ? "true" : "false");
            break;
        case HASP_ATTR_TYPE_STR:
            ESP_LOGI(TAG, "get %s (str)", attribute); /* 3f will echo value */
            break;
        case HASP_ATTR_TYPE_COLOR:
            /* Written OK, or a get on write=false — nothing to log for MVP. */
            break;
        case HASP_ATTR_TYPE_COLOR_INVALID:
            ESP_LOGW(TAG, "color parse failed for '%s' payload='%s'", attribute, payload);
            break;
        case HASP_ATTR_TYPE_ALIGN_INVALID:
            ESP_LOGW(TAG, "invalid align '%s'", payload);
            break;
        case HASP_ATTR_TYPE_LONG_MODE_INVALID:
            ESP_LOGW(TAG, "invalid long-mode '%s'", payload);
            break;
        case HASP_ATTR_TYPE_RANGE_ERROR:
            ESP_LOGW(TAG, "range error on '%s' payload='%s'", attribute, payload);
            break;
        case HASP_ATTR_TYPE_METHOD_INVALID_FOR_PAGE:
            ESP_LOGE(TAG, "'%s' not valid on a page", attribute);
            break;
        case HASP_ATTR_TYPE_METHOD_OK:
            break;
        default:
            ESP_LOGW(TAG, "unhandled ret=%d for '%s'", (int)ret, attribute);
    }
}
