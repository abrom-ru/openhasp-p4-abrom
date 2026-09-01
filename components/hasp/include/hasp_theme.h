/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* p4-abrom step 3h-3a — theme dispatcher.
 * Mirrors S3 hasp.cpp lines 82-85 (globals) and 479-554 (hasp_set_theme
 * switch). LVGL 9 collapses S3's per-widget theme_apply into a single
 * default theme (lv_theme_default) parameterised by primary/secondary/dark;
 * per-widget hasp look layered later in 3h-3b via lv_theme_set_apply_cb. */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t    haspThemeId;
extern uint16_t   haspThemeHue;
extern lv_color_t color_primary;
extern lv_color_t color_secondary;

/**
 * Apply theme `themeid` to the default display. Must be called after LVGL
 * init and BEFORE any hasp objects/pages are created (matches S3 haspSetup
 * ordering: theme first, then hasp_init → pages).
 *
 * Supported ids (S3-compatible):
 *   0 = empty (mapped to LVGL 9 simple)
 *   1 = hasp light
 *   2 = hasp dark        [default HASP_THEME_ID]
 *   3 = mono             [needs CONFIG_LV_USE_THEME_MONO=y — currently no-op]
 *   4 = material light
 *   5 = material dark
 *   8 = legacy alias → 1
 *   9 = legacy alias → 5
 */
void hasp_set_theme(uint8_t themeid);

#ifdef __cplusplus
}
#endif
