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
#include "hasp_font.h"

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
        /* 3h-4: arc/dropdown/roller. Remaining (gauge/spinbox/tabview) come with
         * their respective widget batches. */
        case LV_HASP_ARC:
            if (update) lv_arc_set_value(obj, val);
            else        val = lv_arc_get_value(obj);
            break;
        case LV_HASP_DROPDOWN:
            if (update) lv_dropdown_set_selected(obj, (uint32_t)val);
            else        val = (int32_t)lv_dropdown_get_selected(obj);
            break;
        case LV_HASP_ROLLER:
            if (update) lv_roller_set_selected(obj, (uint32_t)val, LV_ANIM_ON);
            else        val = (int32_t)lv_roller_get_selected(obj);
            break;
        /* 3h-4 batch 2: spinbox + tabview. */
        case LV_HASP_SPINBOX:
            if (update) lv_spinbox_set_value(obj, val);
            else        val = lv_spinbox_get_value(obj);
            break;
        case LV_HASP_TABVIEW:
            if (update) lv_tabview_set_active(obj, (uint32_t)val, LV_ANIM_ON);
            else        val = (int32_t)lv_tabview_get_tab_active(obj);
            break;
        default:
            return HASP_ATTR_TYPE_NOT_FOUND;
    }
    return HASP_ATTR_TYPE_INT;
}

/* ==================== common: range (S3:2233) ==================== */

static bool obj_get_range_mvp(lv_obj_t* obj, int32_t& min, int32_t& max)
{
    switch (obj_get_type(obj)) {
        case LV_HASP_SLIDER:  min = lv_slider_get_min_value(obj);  max = lv_slider_get_max_value(obj);  return true;
        case LV_HASP_BAR:     min = lv_bar_get_min_value(obj);     max = lv_bar_get_max_value(obj);     return true;
        case LV_HASP_ARC:     min = lv_arc_get_min_value(obj);     max = lv_arc_get_max_value(obj);     return true;
        case LV_HASP_SPINBOX: min = lv_spinbox_get_min_value(obj); max = lv_spinbox_get_max_value(obj); return true;
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
        case LV_HASP_ARC:
            if (update && (set_min ? val : min) == (set_max ? val : max)) return HASP_ATTR_TYPE_RANGE_ERROR;
            if (update) lv_arc_set_range(obj, set_min ? val : min, set_max ? val : max);
            else        val = set_min ? min : max;
            break;
        case LV_HASP_SPINBOX:
            if (update && (set_min ? val : min) == (set_max ? val : max)) return HASP_ATTR_TYPE_RANGE_ERROR;
            if (update) lv_spinbox_set_range(obj, set_min ? val : min, set_max ? val : max);
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
        case LV_HASP_TEXTAREA:
            if (attr_hash == ATTR_TEXT || attr_hash == ATTR_TXT) {
                if (update) lv_textarea_set_text(obj, payload);
                return HASP_ATTR_TYPE_STR;
            }
            break;
        /* 3h-4 batch 2: MSGBOX. LVGL 9 has no direct set_text; each call to
         * add_text creates a new label. To emulate S3 set-once semantics we
         * reuse the first label under content, or create it lazily. */
        case LV_HASP_MSGBOX:
            if (attr_hash == ATTR_TEXT || attr_hash == ATTR_TXT) {
                if (update) {
                    lv_obj_t* content = lv_msgbox_get_content(obj);
                    lv_obj_t* first   = content ? lv_obj_get_child(content, 0) : nullptr;
                    if (first) lv_label_set_text(first, payload);
                    else       lv_msgbox_add_text(obj, payload);
                }
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

/* ==================== local style: full table (S3:721 → LVGL 9) ==============
 * 3g scope: expand attribute_local_style from 2-color stub → LVGL 9 port of
 * `hasp_local_style_attr()` (S3 hasp_attribute.cpp:721..1200-ish, ~60 style
 * props). We keep S3 semantics (parse payload → lv_obj_set_style_*, return
 * HASP_ATTR_TYPE_*).
 *
 * 3h-1 scope: replace hard-coded LV_PART_MAIN selector with a proper part/
 * state parser. Ports S3 `hasp_attribute_get_part_state{_new,_old}` (S3:347/
 * 531/702) to LVGL 9. Attributes like "bg_color1" (indicator on slider/switch/
 * bar/checkbox), "bg_color20" (knob on slider/switch/arc — new 2-digit format:
 * PS with P=part_num tens, S=state units), or "bg_color12" (indicator +
 * PRESSED) now hit the right LVGL 9 part/state combination.
 *
 * Widgets covered by the part table: BUTTON, LABEL, SWITCH, SLIDER, CHECKBOX,
 * BAR (currently registered in hasp_object) + BTNMATRIX/ARC/SPINNER/ROLLER/
 * DROPDOWN cases kept 1-в-1 with S3 so they Just Work when those widgets land
 * in 3h-4. Types not in this table default to LV_PART_MAIN.
 *
 * NOT ported (deferred):
 *   - text_font — needs font subsystem (FreeType + openhasp.ttf blob), 3h.
 *   - pattern_* — LVGL 9 has no direct pattern; bg_image_* is the replacement
 *     but the ATTR_ hashes map to LVGL 7 names; wire separately when needed.
 *   - value_* — LVGL 7-only style category; no LVGL 9 equivalent.
 *   - scale_* — arc/meter scales, land with 3h widget expansion.
 *   - transform angle/zoom → LVGL 9 has rotation/scale_x/scale_y (different
 *     API); wire when we add images.
 *   - blend_mode branches — rarely used, deferred.
 *   - get path (update=false) publishes nothing yet — MQTT get pipe is step 4.
 *
 * LVGL 7 → 9 name changes applied here:
 *   shadow_ofs_x/y   → shadow_offset_x/y
 *   img_opa/recolor  → image_opa/recolor
 *   opa_scale        → opa (same effect since v8)
 *   pad_inner        → pad_row + pad_column (LVGL 9 splits axes)
 */

/* Payload → lv_color_t. Returns false on parse failure. */
static bool parse_color(const char* payload, lv_color_t& out)
{
    lv_color32_t c;
    if (!Parser::haspPayloadToColor(payload, c)) return false;
    out = lv_color_make(c.red, c.green, c.blue);
    return true;
}

/* Payload → lv_grad_dir_t (accepts "none"/"hor"/"ver" or numeric 0/1/2). */
static lv_grad_dir_t parse_grad_dir(const char* payload)
{
    if      (!strcasecmp(payload, "none")) return LV_GRAD_DIR_NONE;
    else if (!strcasecmp(payload, "hor"))  return LV_GRAD_DIR_HOR;
    else if (!strcasecmp(payload, "ver"))  return LV_GRAD_DIR_VER;
    return (lv_grad_dir_t)atoi(payload);
}

/* Payload → lv_border_side_t. Accepts named ("top"/"bottom"/"left"/"right"/
 * "full"/"none") or numeric mask (S3 style — 0=NONE, 15=FULL). */
static lv_border_side_t parse_border_side(const char* payload)
{
    if      (!strcasecmp(payload, "none"))   return LV_BORDER_SIDE_NONE;
    else if (!strcasecmp(payload, "top"))    return LV_BORDER_SIDE_TOP;
    else if (!strcasecmp(payload, "bottom")) return LV_BORDER_SIDE_BOTTOM;
    else if (!strcasecmp(payload, "left"))   return LV_BORDER_SIDE_LEFT;
    else if (!strcasecmp(payload, "right"))  return LV_BORDER_SIDE_RIGHT;
    else if (!strcasecmp(payload, "full"))   return LV_BORDER_SIDE_FULL;
    return (lv_border_side_t)atoi(payload);
}

/* Payload → lv_text_decor_t bitmask. Accepts named ("none"/"underline"/
 * "strikethrough") or numeric (S3: 0/1/2). */
static lv_text_decor_t parse_text_decor(const char* payload)
{
    if      (!strcasecmp(payload, "none"))          return LV_TEXT_DECOR_NONE;
    else if (!strcasecmp(payload, "underline"))     return LV_TEXT_DECOR_UNDERLINE;
    else if (!strcasecmp(payload, "strikethrough")) return LV_TEXT_DECOR_STRIKETHROUGH;
    return (lv_text_decor_t)atoi(payload);
}

/* Returns true iff `s` is non-empty AND every char is 0..9. Empty string →
 * true (matches Parser::is_only_digits semantics from S3 hasp_parser.cpp:37;
 * split_payload relies on this for the trailing-empty-position return). */
static bool str_is_only_digits(const char* s)
{
    if (!*s) return true;
    while (*s) { if (*s < '0' || *s > '9') return false; s++; }
    return true;
}

/* S3 hasp_attribute_split_payload — scan char-by-char, return the position of
 * the first substring that is only digits. Returns strlen(payload) when the
 * attribute has no digit suffix. */
static size_t hasp_attribute_split_payload(const char* payload)
{
    size_t pos = 0;
    while (payload[pos] != '\0') {
        if (str_is_only_digits(payload + pos)) return pos;
        pos++;
    }
    return pos;
}

/* Map hasp part-num (LV_HASP_PART_* — 0/10/20/30/40/50/60/70/80/90) + widget
 * type to LVGL 9 lv_part_t. Mirrors the widget-type switch in
 * S3 hasp_attribute_get_part_state_new (S3 hasp_attribute.cpp:400..528). */
static lv_part_t hasp_part_to_lv_part(lv_obj_t* obj, uint8_t part_num)
{
    switch (obj_get_type(obj)) {
        case LV_HASP_SLIDER:
        case LV_HASP_SWITCH:
        case LV_HASP_ARC:
            switch (part_num) {
                case LV_HASP_PART_INDICATOR: return LV_PART_INDICATOR;
                case LV_HASP_PART_KNOB:      return LV_PART_KNOB;
                default:                     return LV_PART_MAIN;
            }
        case LV_HASP_BAR:
        case LV_HASP_SPINNER:
            return (part_num == LV_HASP_PART_INDICATOR) ? LV_PART_INDICATOR : LV_PART_MAIN;
        case LV_HASP_CHECKBOX:
            return (part_num == LV_HASP_PART_INDICATOR) ? LV_PART_INDICATOR : LV_PART_MAIN;
        case LV_HASP_ROLLER:
            return (part_num == LV_HASP_PART_SELECTED) ? LV_PART_SELECTED : LV_PART_MAIN;
        case LV_HASP_DROPDOWN:
            switch (part_num) {
                case LV_HASP_PART_ITEMS:     return LV_PART_ITEMS;
                case LV_HASP_PART_SELECTED:  return LV_PART_SELECTED;
                case LV_HASP_PART_SCROLLBAR: return LV_PART_SCROLLBAR;
                default:                     return LV_PART_MAIN;
            }
        case LV_HASP_BTNMATRIX:
            return (part_num == LV_HASP_PART_ITEMS) ? LV_PART_ITEMS : LV_PART_MAIN;
        /* 3h-4 batch 2. LVGL 9 dropped LV_PART_TICKS (was LVGL 7-only for chart);
         * scale/tick styling is done via LV_PART_MAIN in v9. */
        case LV_HASP_CHART:
            switch (part_num) {
                case LV_HASP_PART_ITEMS:  return LV_PART_ITEMS;
                case LV_HASP_PART_CURSOR: return LV_PART_CURSOR;
                default:                  return LV_PART_MAIN;
            }
        case LV_HASP_TABLE:
        case LV_HASP_TABVIEW:
            return (part_num == LV_HASP_PART_ITEMS) ? LV_PART_ITEMS : LV_PART_MAIN;
        default:
            return LV_PART_MAIN;
    }
}

/* New format (2-digit trailing) — S3 hasp_attribute_get_part_state_new
 * (S3:347). Attribute like "bg_color12" splits to attr_out="bg_color",
 * index=12 → state_num=2 (PRESSED), part_num=10 (INDICATOR). */
static void hasp_attr_split_new(lv_obj_t* obj, const char* attr_in, char* attr_out, size_t out_sz,
                                lv_part_t& part, lv_state_t& state)
{
    state = LV_STATE_DEFAULT;
    part  = LV_PART_MAIN;

    size_t pos = hasp_attribute_split_payload(attr_in);
    if (pos == 0 || pos >= out_sz) { attr_out[0] = 0; return; }
    strncpy(attr_out, attr_in, pos);
    attr_out[pos] = 0;

    int index         = atoi(attr_in + pos);
    uint8_t state_num = (uint8_t)(index % 10);
    uint8_t part_num  = (uint8_t)(index - state_num);

    switch (state_num) {
        case 1:  state = LV_STATE_CHECKED; break;
        case 2:  state = LV_STATE_PRESSED; break;
        case 3:  state = (lv_state_t)(LV_STATE_PRESSED | LV_STATE_CHECKED); break;
        case 4:  state = LV_STATE_DISABLED; break;
        case 5:  state = (lv_state_t)(LV_STATE_DISABLED | LV_STATE_CHECKED); break;
        default: state = LV_STATE_DEFAULT;
    }
    part = hasp_part_to_lv_part(obj, part_num);
}

/* Old format (single-digit trailing or none) — S3 hasp_attribute_get_part_
 * state_old (S3:531). Widget-specific: on SLIDER "bg_color1" = INDICATOR,
 * "bg_color2" = KNOB. On BUTTON "bg_color1" = CHECKED state on MAIN. */
static void hasp_attr_split_old(lv_obj_t* obj, const char* attr_in, char* attr_out, size_t out_sz,
                                lv_part_t& part, lv_state_t& state)
{
    state = LV_STATE_DEFAULT;
    part  = LV_PART_MAIN;

    int len = (int)strlen(attr_in);
    if (len <= 0 || (size_t)len >= out_sz) { attr_out[0] = 0; return; }

    int index = atoi(&attr_in[len - 1]);
    if (attr_in[len - 1] == '0') {
        len--;
    } else if (index > 0) {
        len--;
    } else {
        index = -1; /* no digit suffix — plain attribute */
    }
    strncpy(attr_out, attr_in, len);
    attr_out[len] = 0;

    switch (obj_get_type(obj)) {
        case LV_HASP_BUTTON:
            switch (index) {
                case 1: state = LV_STATE_CHECKED; break;
                case 2: state = LV_STATE_PRESSED; break;
                case 3: state = (lv_state_t)(LV_STATE_PRESSED | LV_STATE_CHECKED); break;
                case 4: state = LV_STATE_DISABLED; break;
                case 5: state = (lv_state_t)(LV_STATE_DISABLED | LV_STATE_CHECKED); break;
                default: break;
            }
            break;
        case LV_HASP_BTNMATRIX:
            switch (index) {
                case 0: part = LV_PART_ITEMS;                                       break;
                case 1: part = LV_PART_ITEMS; state = LV_STATE_CHECKED;             break;
                case 2: part = LV_PART_ITEMS; state = LV_STATE_PRESSED;             break;
                case 3: part = LV_PART_ITEMS; state = (lv_state_t)(LV_STATE_PRESSED | LV_STATE_CHECKED); break;
                case 4: part = LV_PART_ITEMS; state = LV_STATE_DISABLED;            break;
                case 5: part = LV_PART_ITEMS; state = (lv_state_t)(LV_STATE_DISABLED | LV_STATE_CHECKED); break;
                default: break;
            }
            break;
        case LV_HASP_SLIDER:
        case LV_HASP_SWITCH:
        case LV_HASP_ARC:
            if      (index == 1) part = LV_PART_INDICATOR;
            else if (index == 2) part = LV_PART_KNOB;
            break;
        case LV_HASP_BAR:
        case LV_HASP_SPINNER:
            if (index == 1) part = LV_PART_INDICATOR;
            break;
        case LV_HASP_CHECKBOX:
            if (index == 1) part = LV_PART_INDICATOR;
            break;
        case LV_HASP_ROLLER:
            if (index == 1) part = LV_PART_SELECTED;
            break;
        default:
            break;
    }
}

/* S3 hasp_attribute_get_part_state (S3:702) — 2-digit trailing suffix picks
 * new format, otherwise old. Populates attr_out (stripped attr name), part,
 * state. Caller must re-hash attr_out via Parser::get_sdbm. */
static void hasp_attr_get_part_state(lv_obj_t* obj, const char* attr_in, char* attr_out, size_t out_sz,
                                     lv_part_t& part, lv_state_t& state)
{
    size_t pos = hasp_attribute_split_payload(attr_in);
    if (strlen(attr_in + pos) == 2)
        hasp_attr_split_new(obj, attr_in, attr_out, out_sz, part, state);
    else
        hasp_attr_split_old(obj, attr_in, attr_out, out_sz, part, state);
}

static hasp_attribute_type_t attribute_local_style(lv_obj_t* obj, const char* attr_p, uint16_t attr_hash,
                                                   const char* payload, bool update)
{
    /* S3 hasp_local_style_attr:731 — strip trailing part/state suffix, rehash
     * the base name, build LVGL 9 selector. */
    char       attr[32];
    lv_part_t  part  = LV_PART_MAIN;
    lv_state_t state = LV_STATE_DEFAULT;
    hasp_attr_get_part_state(obj, attr_p, attr, sizeof(attr), part, state);
    if (attr[0]) attr_hash = Parser::get_sdbm(attr);
    /* LVGL 9 declares lv_part_t and lv_state_t as strong enums — direct
     * `part | state` triggers -Werror=deprecated-enum-enum-conversion.
     * Cast to uint32_t first (matches lv_style_selector_t underlying type). */
    const lv_style_selector_t sel = (lv_style_selector_t)((uint32_t)part | (uint32_t)state);

    /* Numeric payload cached once — most branches want an int/uint8 val. */
    long ival = strtol(payload, nullptr, 10);

    switch (attr_hash) {

        /* ---------- Object / general ---------- */
        case ATTR_RADIUS:
            if (update) lv_obj_set_style_radius(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;

        case ATTR_CLIP_CORNER:
            if (update) lv_obj_set_style_clip_corner(obj, ival != 0, sel);
            return HASP_ATTR_TYPE_BOOL;

        case ATTR_OPA_SCALE: /* LVGL 7 alias — LVGL 9 uses opa uniformly. */
            if (update) lv_obj_set_style_opa(obj, (lv_opa_t)ival, sel);
            return HASP_ATTR_TYPE_INT;

        case ATTR_TRANSFORM_WIDTH:
            if (update) lv_obj_set_style_transform_width(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;

        case ATTR_TRANSFORM_HEIGHT:
            if (update) lv_obj_set_style_transform_height(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;

        /* ---------- Background ---------- */
        case ATTR_BG_COLOR: {
            if (update) {
                lv_color_t col;
                if (!parse_color(payload, col)) return HASP_ATTR_TYPE_COLOR_INVALID;
                lv_obj_set_style_bg_color(obj, col, sel);
                /* Default bg_opa on unstyled objects is 0 → color would be invisible.
                 * S3 also forced OPA_COVER here for convenience (hasp_attribute.cpp:781). */
                lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
            }
            return HASP_ATTR_TYPE_COLOR;
        }
        case ATTR_BG_GRAD_COLOR: {
            if (update) {
                lv_color_t col;
                if (!parse_color(payload, col)) return HASP_ATTR_TYPE_COLOR_INVALID;
                lv_obj_set_style_bg_grad_color(obj, col, sel);
            }
            return HASP_ATTR_TYPE_COLOR;
        }
        case ATTR_BG_OPA:
            if (update) lv_obj_set_style_bg_opa(obj, (lv_opa_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_BG_GRAD_DIR:
            if (update) lv_obj_set_style_bg_grad_dir(obj, parse_grad_dir(payload), sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_BG_MAIN_STOP:
            if (update) lv_obj_set_style_bg_main_stop(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_BG_GRAD_STOP:
            if (update) lv_obj_set_style_bg_grad_stop(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;

        /* ---------- Padding (LVGL 9 splits pad_inner into pad_row+pad_column) ---- */
        case ATTR_PAD_TOP:
            if (update) lv_obj_set_style_pad_top(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_PAD_BOTTOM:
            if (update) lv_obj_set_style_pad_bottom(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_PAD_LEFT:
            if (update) lv_obj_set_style_pad_left(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_PAD_RIGHT:
            if (update) lv_obj_set_style_pad_right(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_PAD_INNER:
            /* LVGL 9 has no single "inner" — apply the same value to both axes. */
            if (update) {
                lv_obj_set_style_pad_row(obj, (int32_t)ival, sel);
                lv_obj_set_style_pad_column(obj, (int32_t)ival, sel);
            }
            return HASP_ATTR_TYPE_INT;

        /* ---------- Margin ---------- */
        case ATTR_MARGIN_TOP:
            if (update) lv_obj_set_style_margin_top(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_MARGIN_BOTTOM:
            if (update) lv_obj_set_style_margin_bottom(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_MARGIN_LEFT:
            if (update) lv_obj_set_style_margin_left(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_MARGIN_RIGHT:
            if (update) lv_obj_set_style_margin_right(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;

        /* ---------- Border ---------- */
        case ATTR_BORDER_WIDTH:
            if (update) lv_obj_set_style_border_width(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_BORDER_COLOR: {
            if (update) {
                lv_color_t col;
                if (!parse_color(payload, col)) return HASP_ATTR_TYPE_COLOR_INVALID;
                lv_obj_set_style_border_color(obj, col, sel);
            }
            return HASP_ATTR_TYPE_COLOR;
        }
        case ATTR_BORDER_OPA:
            if (update) lv_obj_set_style_border_opa(obj, (lv_opa_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_BORDER_SIDE:
            if (update) lv_obj_set_style_border_side(obj, parse_border_side(payload), sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_BORDER_POST:
            if (update) lv_obj_set_style_border_post(obj, ival != 0, sel);
            return HASP_ATTR_TYPE_BOOL;

        /* ---------- Outline ---------- */
        case ATTR_OUTLINE_WIDTH:
            if (update) lv_obj_set_style_outline_width(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_OUTLINE_PAD:
            if (update) lv_obj_set_style_outline_pad(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_OUTLINE_COLOR: {
            if (update) {
                lv_color_t col;
                if (!parse_color(payload, col)) return HASP_ATTR_TYPE_COLOR_INVALID;
                lv_obj_set_style_outline_color(obj, col, sel);
            }
            return HASP_ATTR_TYPE_COLOR;
        }
        case ATTR_OUTLINE_OPA:
            if (update) lv_obj_set_style_outline_opa(obj, (lv_opa_t)ival, sel);
            return HASP_ATTR_TYPE_INT;

        /* ---------- Shadow (LVGL 9: ofs → offset) ---------- */
        case ATTR_SHADOW_WIDTH:
            if (update) lv_obj_set_style_shadow_width(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_SHADOW_OFS_X:
            if (update) lv_obj_set_style_shadow_offset_x(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_SHADOW_OFS_Y:
            if (update) lv_obj_set_style_shadow_offset_y(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_SHADOW_SPREAD:
            if (update) lv_obj_set_style_shadow_spread(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_SHADOW_COLOR: {
            if (update) {
                lv_color_t col;
                if (!parse_color(payload, col)) return HASP_ATTR_TYPE_COLOR_INVALID;
                lv_obj_set_style_shadow_color(obj, col, sel);
            }
            return HASP_ATTR_TYPE_COLOR;
        }
        case ATTR_SHADOW_OPA:
            if (update) lv_obj_set_style_shadow_opa(obj, (lv_opa_t)ival, sel);
            return HASP_ATTR_TYPE_INT;

        /* ---------- Text (font deferred — needs FreeType, 3h) ---------- */
        case ATTR_TEXT_COLOR: {
            if (update) {
                lv_color_t col;
                if (!parse_color(payload, col)) return HASP_ATTR_TYPE_COLOR_INVALID;
                lv_obj_set_style_text_color(obj, col, sel);
            }
            return HASP_ATTR_TYPE_COLOR;
        }
        case ATTR_TEXT_OPA:
            if (update) lv_obj_set_style_text_opa(obj, (lv_opa_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_TEXT_LETTER_SPACE:
            if (update) lv_obj_set_style_text_letter_space(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_TEXT_LINE_SPACE:
            if (update) lv_obj_set_style_text_line_space(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_TEXT_DECOR:
            if (update) lv_obj_set_style_text_decor(obj, parse_text_decor(payload), sel);
            return HASP_ATTR_TYPE_INT;

        /* ---------- Text font (3h-2 stage 4) ---------- */
        /* Mirrors S3 hasp_attribute.cpp:890 (ATTR_TEXT_FONT in hasp_local_style_attr).
         * Payload is delegated to get_font() (hasp_font.cpp): numeric → default
         * openhasp.ttf @ size; "<name><size>" → /littlefs/<name>.ttf @ size.
         * S3's LVGL 7-only fallbacks (hasp_get_font 0..7, unscii_8_icon,
         * HASP_FONT_1..5) are intentionally NOT ported — the p4 build has no
         * built-in bitmap fonts registered; every text_font resolves through
         * FreeType.  S3 also set the same font on LV_DROPDOWN_PART_LIST/
         * SELECTED for consistency; deferred here (dropdown-list styling is
         * not in the 3h-4 batch 1 smoke path). */
        case ATTR_TEXT_FONT: {
            if (!update) return HASP_ATTR_TYPE_STR;
            lv_font_t* font = get_font(payload);
            if (!font) return HASP_ATTR_TYPE_STR;   /* logged inside get_font */
            lv_obj_set_style_text_font(obj, font, sel);
            return HASP_ATTR_TYPE_STR;
        }

        /* ---------- Line ---------- */
        case ATTR_LINE_WIDTH:
            if (update) lv_obj_set_style_line_width(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_LINE_DASH_WIDTH:
            if (update) lv_obj_set_style_line_dash_width(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_LINE_DASH_GAP:
            if (update) lv_obj_set_style_line_dash_gap(obj, (int32_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_LINE_ROUNDED:
            if (update) lv_obj_set_style_line_rounded(obj, ival != 0, sel);
            return HASP_ATTR_TYPE_BOOL;
        case ATTR_LINE_COLOR: {
            if (update) {
                lv_color_t col;
                if (!parse_color(payload, col)) return HASP_ATTR_TYPE_COLOR_INVALID;
                lv_obj_set_style_line_color(obj, col, sel);
            }
            return HASP_ATTR_TYPE_COLOR;
        }
        case ATTR_LINE_OPA:
            if (update) lv_obj_set_style_line_opa(obj, (lv_opa_t)ival, sel);
            return HASP_ATTR_TYPE_INT;

        /* ---------- Image (LVGL 9 renamed img_ → image_) ---------- */
        case ATTR_IMAGE_RECOLOR: {
            if (update) {
                lv_color_t col;
                if (!parse_color(payload, col)) return HASP_ATTR_TYPE_COLOR_INVALID;
                lv_obj_set_style_image_recolor(obj, col, sel);
            }
            return HASP_ATTR_TYPE_COLOR;
        }
        case ATTR_IMAGE_OPA:
            if (update) lv_obj_set_style_image_opa(obj, (lv_opa_t)ival, sel);
            return HASP_ATTR_TYPE_INT;
        case ATTR_IMAGE_RECOLOR_OPA:
            if (update) lv_obj_set_style_image_recolor_opa(obj, (lv_opa_t)ival, sel);
            return HASP_ATTR_TYPE_INT;

        default:
            return HASP_ATTR_TYPE_NOT_FOUND;
    }
}

/* ==================== 3h-4: widget-specific string/points attrs ==================== */

/* ATTR_OPTIONS — dropdown/roller only. Payload is the newline-separated option
 * list. Roller uses NORMAL mode (single-cycle). Passing a copy via set_options
 * (not _static) so the caller's payload can be freed. */
static hasp_attribute_type_t attribute_options(lv_obj_t* obj, const char* payload, bool update)
{
    if (!update) return HASP_ATTR_TYPE_STR; /* get path — MQTT wired later */
    switch (obj_get_type(obj)) {
        case LV_HASP_DROPDOWN: lv_dropdown_set_options(obj, payload); return HASP_ATTR_TYPE_STR;
        case LV_HASP_ROLLER:   lv_roller_set_options(obj, payload, LV_ROLLER_MODE_NORMAL); return HASP_ATTR_TYPE_STR;
        default: return HASP_ATTR_TYPE_NOT_FOUND;
    }
}

/* ATTR_SRC — image only (MVP). Accepts a symbol pointer (e.g. LV_SYMBOL_OK)
 * or a path string. LVGL 9 lv_image_set_src copies the pointer as-is; string
 * payloads coming from JSON get copied by ArduinoJson, so re-strdup here to
 * pin lifetime. Freed via `extra`. */
static hasp_attribute_type_t attribute_src(lv_obj_t* obj, const char* payload, bool update)
{
    if (obj_get_type(obj) != LV_HASP_IMAGE) return HASP_ATTR_TYPE_NOT_FOUND;
    if (!update) return HASP_ATTR_TYPE_STR;

    hasp_obj_user_data_t* ud = hasp_obj_ud(obj);
    if (!ud) return HASP_ATTR_TYPE_NOT_FOUND;

    char* dup = strdup(payload);
    if (!dup) return HASP_ATTR_TYPE_NOT_FOUND;

    if (ud->extra) free(ud->extra);
    ud->extra = dup;
    lv_image_set_src(obj, dup);
    return HASP_ATTR_TYPE_STR;
}

/* ATTR_POINTS — line only. Payload format "x1,y1;x2,y2;…" (S3 uses JSON-array
 * via hasp_new_object nested path; MVP accepts the semicolon form). Allocates
 * lv_point_precise_t[] and stashes in ud->extra so delete_event_handler frees. */
static hasp_attribute_type_t attribute_points(lv_obj_t* obj, const char* payload, bool update)
{
    if (obj_get_type(obj) != LV_HASP_LINE) return HASP_ATTR_TYPE_NOT_FOUND;
    if (!update) return HASP_ATTR_TYPE_STR;

    hasp_obj_user_data_t* ud = hasp_obj_ud(obj);
    if (!ud) return HASP_ATTR_TYPE_NOT_FOUND;

    /* First pass: count pairs. */
    uint32_t pairs = 1;
    for (const char* p = payload; *p; p++) if (*p == ';') pairs++;

    lv_point_precise_t* pts = (lv_point_precise_t*)calloc(pairs, sizeof(lv_point_precise_t));
    if (!pts) return HASP_ATTR_TYPE_NOT_FOUND;

    uint32_t i = 0;
    const char* p = payload;
    while (*p && i < pairs) {
        char* end;
        long x = strtol(p, &end, 10);
        if (end == p || *end != ',') { free(pts); return HASP_ATTR_TYPE_NOT_FOUND; }
        p = end + 1;
        long y = strtol(p, &end, 10);
        if (end == p) { free(pts); return HASP_ATTR_TYPE_NOT_FOUND; }
        pts[i].x = (lv_value_precise_t)x;
        pts[i].y = (lv_value_precise_t)y;
        i++;
        p = (*end == ';') ? end + 1 : end;
    }

    if (ud->extra) free(ud->extra);
    ud->extra = pts;
    lv_line_set_points(obj, pts, i);
    return HASP_ATTR_TYPE_STR;
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

        /* 3h-4: widget-specific string / points attributes. */
        case ATTR_OPTIONS:
            ret = attribute_options(obj, payload, update);
            break;
        case ATTR_SRC:
            ret = attribute_src(obj, payload, update);
            break;
        case ATTR_POINTS:
            ret = attribute_points(obj, payload, update);
            break;

        case ATTR_COMMENT:
            ret = HASP_ATTR_TYPE_METHOD_OK;
            break;

        default:
            /* Fall through to local-style / per-widget attributes. Pass raw
             * attribute string so the callee can strip part/state suffix and
             * rehash the base name (3h-1). */
            ret = attribute_local_style(obj, attribute, attr_hash, payload, update);
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
