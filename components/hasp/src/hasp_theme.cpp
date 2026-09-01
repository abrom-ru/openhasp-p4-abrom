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

/* Extension point for 3h-3b: layer hasp-specific styles on top of the base
 * LVGL 9 theme. Same intent as S3 hasp.cpp:464 custom_font_apply_cb, but
 * LVGL 9 dropped lv_theme_style_t; dispatch by lv_obj_check_type().
 * Currently a no-op — kept wired so 3h-3b only needs to fill the body. */
static void hasp_theme_apply_cb(lv_theme_t* /*th*/, lv_obj_t* /*obj*/)
{
    /* 3h-3b will add per-widget style tweaks here, e.g.:
     *   if (lv_obj_check_type(obj, &lv_button_class)) { ... }
     *   if (lv_obj_check_type(obj, &lv_slider_class)) { ... }
     */
}

/* Wrap base theme in a child theme via lv_theme_set_parent so our apply_cb
 * runs after the base's — LVGL 9's supported layering pattern (see
 * lv_theme.h:59-66). Avoids poking th->apply_cb which lives in the private
 * header. Child theme is created once and reused. */
static lv_theme_t* s_hasp_theme = NULL;
static lv_theme_t* wrap_with_hasp_layer(lv_theme_t* base)
{
    if (!base) return NULL;
    if (!s_hasp_theme) {
        s_hasp_theme = lv_theme_create();
        if (!s_hasp_theme) return base;
        lv_theme_set_apply_cb(s_hasp_theme, hasp_theme_apply_cb);
    }
    lv_theme_set_parent(s_hasp_theme, base);
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
