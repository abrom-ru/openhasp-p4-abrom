/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* p4-abrom step 3h-3a — theme dispatcher implementation.
 *
 * S3 (LVGL 6/7) has lv_theme_hasp.c (~1500 lines, 30+ per-widget _init).
 * LVGL 9 removed lv_style_int_t / lv_theme_style_t / per-widget theme_apply
 * hooks that the S3 file depended on, so a 1-to-1 port is not possible.
 * Instead we use LVGL 9's built-in themes (default/simple) with our colour
 * knobs, matching S3's public dispatcher signature so downstream code
 * (config load, dispatch commands) is unaffected.
 *
 * Legacy widgets referenced by S3 (gauge / linemeter / cpicker) do not
 * exist in LVGL 9 and are intentionally not restored.
 */

#include "hasp_theme.h"

#include "esp_log.h"

static const char* TAG = "hasp_theme";

/* Default theme id — S3 hasp.h defines HASP_THEME_ID as 2 (dark). */
#ifndef HASP_THEME_ID
#define HASP_THEME_ID 2
#endif

uint8_t    haspThemeId    = HASP_THEME_ID;
uint16_t   haspThemeHue   = 200;
/* Real hsv-to-rgb runs at hasp_set_theme() time; static init just needs a
 * defined value. S3 did the call at static-init but LVGL 9 static color
 * literals are simpler and equivalent (both get overwritten before use). */
lv_color_t color_primary   = LV_COLOR_MAKE(0x00, 0x80, 0xff);
lv_color_t color_secondary = LV_COLOR_MAKE(0x00, 0x80, 0xff);

/* Tracks base-theme "dark" flag so apply_cb can pick knob/text colors that
 * match S3 IS_LIGHT branches. Set by hasp_set_theme() before the base theme
 * is wrapped. */
static bool s_is_dark = true;

/* 3h-3b: layer hasp-specific styles on top of the base LVGL 9 theme.
 * Values ported 1-to-1 from S3 lv_theme_hasp.c per-widget _init() bodies.
 * Only widgets where default LVGL 9 theme visually differs from S3 hasp
 * get overrides here — button/label/bar/dropdown/roller/textarea/spinner
 * already look correct with lv_theme_default + color_primary.
 *
 * S3 references (openhasp-abrom/src/hasp/lv_theme_hasp.c):
 *   led_init      lines 518-533
 *   slider_init   lines 535-550
 *   switch_init   lines 552-563
 *   arc_init      lines 620-648
 *
 * LVGL 9 API notes vs S3 (LVGL 7):
 *   - LV_STATE_DEFAULT baked into selector 0 → pass just LV_PART_MAIN etc.
 *   - lv_style_set_pad_all → lv_obj_set_style_pad_all (per-obj, not style)
 *   - Slider/switch/arc "knob" is a PART, not a separate style bucket
 *   - LV_COLOR_WHITE/GRAY macros → lv_color_white()/lv_palette_main() */
static void hasp_theme_apply_cb(lv_theme_t* /*th*/, lv_obj_t* obj)
{
    const lv_color_t knob_color = s_is_dark ? lv_color_white()
                                            : lv_palette_main(LV_PALETTE_GREY);

#if LV_USE_LED
    if (lv_obj_check_type(obj, &lv_led_class)) {
        /* LVGL 9 default already gives LED: bg white→grey grad, radius CIRCLE,
         * shadow 15/spread 5 in white. lv_led_event then repaints everything
         * through led->color (theme primary). Do nothing here to inspect the
         * clean default first — if it looks wrong, we know it's not our layer. */
        return;
    }
#endif

#if LV_USE_SLIDER
    if (lv_obj_check_type(obj, &lv_slider_class)) {
        /* S3 slider_init: knob bg WHITE (dark) / GRAY (light), pad_all 7. */
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_bg_color(obj, knob_color, LV_PART_KNOB);
        lv_obj_set_style_pad_all(obj, LV_DPX(7), LV_PART_KNOB);
        return;
    }
#endif

#if LV_USE_SWITCH
    if (lv_obj_check_type(obj, &lv_switch_class)) {
        /* S3 switch_init: knob bg WHITE (dark) / GRAY (light), pad_all 2. */
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_bg_color(obj, knob_color, LV_PART_KNOB);
        lv_obj_set_style_pad_all(obj, LV_DPX(2), LV_PART_KNOB);
        return;
    }
#endif

#if LV_USE_ARC
    if (lv_obj_check_type(obj, &lv_arc_class)) {
        /* S3 arc_init: indic line=primary, bg+indic 25px rounded, knob white
         * pad 1. arc_bg border_width=25 in S3 was a leftover from old shape
         * math and is a no-op on LVGL 9 arcs — skipped. */
        lv_obj_set_style_arc_color(obj, color_primary, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(obj, LV_DPX(25), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(obj, true, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(obj, LV_DPX(25), LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(obj, true, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_bg_color(obj, knob_color, LV_PART_KNOB);
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_pad_all(obj, LV_DPX(1), LV_PART_KNOB);
        return;
    }
#endif

    /* Checkbox: S3 uses pattern_image+pattern_recolor for the checkmark
     * (LVGL 7 API removed in 9). Default LVGL 9 theme already draws the
     * OK glyph as bg_image on LV_PART_INDICATOR when checked, so we only
     * push color_primary into the checked-state bg to match S3 hue. */
#if LV_USE_CHECKBOX
    if (lv_obj_check_type(obj, &lv_checkbox_class)) {
        /* LVGL 9 strong enums — same cast as hasp_attribute.cpp:653. */
        lv_obj_set_style_bg_color(obj, color_primary,
                                  (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR |
                                                        (uint32_t)LV_STATE_CHECKED));
        return;
    }
#endif
}

/* Wrap base theme in a child theme via lv_theme_set_parent so our apply_cb
 * runs after the base's — LVGL 9's supported layering pattern (see
 * lv_theme.h:59-66). Avoids poking th->apply_cb which lives in the private
 * header. Child theme is created once and reused.
 *
 * ⚠️ color_primary/secondary and font_* are copied from base at every wrap.
 * Reason: LVGL 9 lv_theme_get_color_primary()/get_font_*() read the top-level
 * theme fields directly and do NOT traverse the parent chain (lv_theme.c:105).
 * lv_led constructor calls lv_theme_get_color_primary(obj) — if we leave the
 * wrapper's fields at their lv_zalloc default (0x000000), lv_led ends up with
 * led->color = black, and lv_led_event redraws the whole widget in near-black
 * regardless of local bg_color from jsonl. Same issue would hit lv_calendar. */
static lv_theme_t* s_hasp_theme = NULL;
static lv_theme_t* wrap_with_hasp_layer(lv_theme_t* base)
{
    if (!base) return NULL;
    if (!s_hasp_theme) {
        s_hasp_theme = lv_theme_create();
        if (!s_hasp_theme) return base;
    }
    /* Copy whole base struct (color_primary/secondary, fonts, flags, disp,
     * user_data, ext_data) so lv_theme_get_color_primary() / get_font_*()
     * / any widget reading th->flags return correct values from our wrapper.
     * These accessors read the top-level theme directly and do NOT traverse
     * the parent chain (lv_theme.c:105-115). Then restore our parent+cb —
     * lv_theme_copy overwrites them with base's. */
    lv_theme_copy(s_hasp_theme, base);
    lv_theme_set_parent(s_hasp_theme, base);
    lv_theme_set_apply_cb(s_hasp_theme, hasp_theme_apply_cb);
    return s_hasp_theme;
}

void hasp_set_theme(uint8_t themeid)
{
    /* Legacy id remap — S3 hasp.cpp:484-486. */
    if (themeid == 8) themeid = 1;
    if (themeid == 9) themeid = 5;
    if (themeid > 5) {
        ESP_LOGE(TAG, "unknown theme id %u", themeid);
        return;
    }

    lv_display_t* disp = lv_display_get_default();
    if (!disp) {
        ESP_LOGE(TAG, "no default display — call hasp_set_theme() after LVGL init");
        return;
    }

    /* Recompute hsv-based primary in case haspThemeHue changed at runtime
     * (S3 hasp.cpp:855 does the same after config load). */
    color_primary = lv_color_hsv_to_rgb(haspThemeHue, 100, 100);

    s_is_dark = (themeid == 2 || themeid == 5);

    lv_theme_t* th = NULL;

    /* IMPORTANT: pass LV_FONT_DEFAULT (Montserrat, from sdkconfig
     * CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14=y), NOT NULL. LVGL 9
     * lv_theme_default sets text_font from the passed font pointer into every
     * per-widget style unconditionally — passing NULL makes label creation
     * crash inside lv_label_event → lv_font_get_line_height(font=NULL).
     * FreeType text_font per label (from 3h-2) overrides this default. */
    switch (themeid) {
        case 0: /* empty → LVGL 9 simple */
#if LV_USE_THEME_SIMPLE
            th = lv_theme_simple_init(disp);
#else
            ESP_LOGW(TAG, "simple theme not enabled");
#endif
            break;

        case 1: /* hasp light */
        case 4: /* material light — same base as hasp light in 3h-3a */
#if LV_USE_THEME_DEFAULT
            th = lv_theme_default_init(disp, color_primary, color_secondary,
                                       /*dark=*/false, /*font=*/LV_FONT_DEFAULT);
#endif
            break;

        case 2: /* hasp dark — default HASP_THEME_ID */
        case 5: /* material dark */
#if LV_USE_THEME_DEFAULT
            th = lv_theme_default_init(disp, color_primary, color_secondary,
                                       /*dark=*/true, /*font=*/LV_FONT_DEFAULT);
#endif
            break;

        case 3: /* mono — LVGL 9 has it but not enabled in current defconfig */
#if LV_USE_THEME_MONO
            th = lv_theme_mono_init(disp, /*dark=*/false, /*font=*/LV_FONT_DEFAULT);
#else
            ESP_LOGW(TAG, "mono theme not enabled (CONFIG_LV_USE_THEME_MONO=n)");
#endif
            break;

        default:
            break;
    }

    if (th) {
        lv_theme_t* wrapped = wrap_with_hasp_layer(th);
        lv_display_set_theme(disp, wrapped);
        ESP_LOGI(TAG, "theme %u applied (primary=%06lx, dark=%d)",
                 themeid,
                 (unsigned long)lv_color_to_u32(color_primary),
                 (themeid == 2 || themeid == 5));
    } else {
        ESP_LOGE(TAG, "theme %u could not be loaded", themeid);
    }
}
