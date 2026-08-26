#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include "lvgl.h"

// De lichtgewicht sleutel-waarde structuur
struct EnumMapping {
    int value;
    const char* str;
};

// 1. Check of een string voorkomt in de opgegeven array (case-insensitive)
template <size_t N>
bool convert_exists(const EnumMapping (&mapping)[N], const char* str) {
    if (!str) return false;
    for (size_t i = 0; i < N; i++) {
        if (strcasecmp(mapping[i].str, str) == 0) {
            return true;
        }
    }
    return false;
}

// 2. String -> Enum (JSON uitlezen). Geeft -1 bij ongeldige invoer.
template <size_t N>
int convert(const EnumMapping (&mapping)[N], const char* str, int fallback = -1) {
    if (!str) return fallback;
    for (size_t i = 0; i < N; i++) {
        if (strcasecmp(mapping[i].str, str) == 0) {
            return mapping[i].value;
        }
    }
    return fallback; 
}

// 3. Enum -> String (JSON schrijven)
template <size_t N>
const char* convert(const EnumMapping (&mapping)[N], int value) {
    for (size_t i = 0; i < N; i++) {
        if (mapping[i].value == value) {
            return mapping[i].str;
        }
    }
    return "unknown"; // Fallback voor JSON export
}

// ==========================================
// CATEGORIE 1: ALGEMENE OBJECT & STIJL ENUMS
// ==========================================

// 1. Base Object Alignment
const EnumMapping array_align[] = {
    {LV_ALIGN_DEFAULT,       "default"},
    {LV_ALIGN_TOP_LEFT,      "top_left"},
    {LV_ALIGN_TOP_MID,       "top_mid"},
    {LV_ALIGN_TOP_RIGHT,     "top_right"},
    {LV_ALIGN_BOTTOM_LEFT,   "bottom_left"},
    {LV_ALIGN_BOTTOM_MID,    "bottom_mid"},
    {LV_ALIGN_BOTTOM_RIGHT,  "bottom_right"},
    {LV_ALIGN_LEFT_MID,      "left_mid"},
    {LV_ALIGN_RIGHT_MID,     "right_mid"},
    {LV_ALIGN_CENTER,        "center"}
};

// 2. Richtingen (Paddings, Layouts, etc.)
const EnumMapping array_dir[] = {
    {LV_DIR_NONE,            "none"},
    {LV_DIR_LEFT,            "left"},
    {LV_DIR_RIGHT,           "right"},
    {LV_DIR_TOP,             "top"},
    {LV_DIR_BOTTOM,          "bottom"},
    {LV_DIR_HOR,             "horizontal"},
    {LV_DIR_VER,             "vertical"},
    {LV_DIR_ALL,             "all"}
};

// 3. Scrollbar Weergave Modus
const EnumMapping array_scrollbar_mode[] = {
    {LV_SCROLLBAR_MODE_OFF,    "off"},
    {LV_SCROLLBAR_MODE_ON,     "on"},
    {LV_SCROLLBAR_MODE_AUTO,   "auto"},
    {LV_SCROLLBAR_MODE_ACTIVE, "active"}
};

// 4. Scroll Snapping Gedrag
const EnumMapping array_scroll_snap[] = {
    {LV_SCROLL_SNAP_NONE,    "none"},
    {LV_SCROLL_SNAP_START,   "start"},
    {LV_SCROLL_SNAP_CENTER,  "center"},
    {LV_SCROLL_SNAP_END,     "end"}
};

// 5. Tekst Uitlijning (Binnen Labels/Knoppen)
const EnumMapping array_text_align[] = {
    {LV_TEXT_ALIGN_LEFT,     "left"},
    {LV_TEXT_ALIGN_CENTER,   "center"},
    {LV_TEXT_ALIGN_RIGHT,    "right"},
    {LV_TEXT_ALIGN_AUTO,     "auto"}
};

// Nieuw: Tekst Decoratie (Bitmask)
const EnumMapping array_text_decor[] = {
    {LV_TEXT_DECOR_NONE,           "none"},
    {LV_TEXT_DECOR_UNDERLINE,      "underline"},
    {LV_TEXT_DECOR_STRIKETHROUGH,  "strikethrough"}
};

// 6. Visuele Blend Modi (Laag-effecten)
const EnumMapping array_blend_mode[] = {
    {LV_BLEND_MODE_NORMAL,      "normal"},
    {LV_BLEND_MODE_ADDITIVE,    "additive"},
    {LV_BLEND_MODE_SUBTRACTIVE, "subtractive"},
    {LV_BLEND_MODE_MULTIPLY,    "multiply"}
};

// 7. Rand Selectie (Border Sides)
const EnumMapping array_border_side[] = {
    {LV_BORDER_SIDE_NONE,     "none"},
    {LV_BORDER_SIDE_TOP,      "top"},
    {LV_BORDER_SIDE_BOTTOM,   "bottom"},
    {LV_BORDER_SIDE_LEFT,     "left"},
    {LV_BORDER_SIDE_RIGHT,    "right"},
    {LV_BORDER_SIDE_INTERNAL, "internal"}
};

// ==========================================
// CATEGORIE 2: FLEX & GRID LAYOUT ENUMS
// ==========================================

// 8. Flex Richtingen (Flow)
const EnumMapping array_flex_flow[] = {
    {LV_FLEX_FLOW_ROW,                 "row"},
    {LV_FLEX_FLOW_COLUMN,              "column"},
    {LV_FLEX_FLOW_ROW_WRAP,            "row_wrap"},
    {LV_FLEX_FLOW_ROW_REVERSE,         "row_reverse"},
    {LV_FLEX_FLOW_ROW_WRAP_REVERSE,    "row_wrap_reverse"},
    {LV_FLEX_FLOW_COLUMN_WRAP,         "column_wrap"},
    {LV_FLEX_FLOW_COLUMN_REVERSE,      "column_reverse"},
    {LV_FLEX_FLOW_COLUMN_WRAP_REVERSE, "column_wrap_reverse"}
};

// 9. Flex Alignment (Main, Cross, Track)
const EnumMapping array_flex_align[] = {
    {LV_FLEX_ALIGN_START,          "start"},
    {LV_FLEX_ALIGN_END,            "end"},
    {LV_FLEX_ALIGN_CENTER,         "center"},
    {LV_FLEX_ALIGN_SPACE_SPACE,    "space"},
    {LV_FLEX_ALIGN_SPACE_EVENLY,   "space_evenly"},
    {LV_FLEX_ALIGN_SPACE_BETWEEN,  "space_between"},
    {LV_FLEX_ALIGN_SPACE_AROUND,   "space_around"}
};

// 10. Grid Alignment (Row & Column)
const EnumMapping array_grid_align[] = {
    {LV_GRID_ALIGN_START,          "start"},
    {LV_GRID_ALIGN_CENTER,         "center"},
    {LV_GRID_ALIGN_END,            "end"},
    {LV_GRID_ALIGN_STRETCH,        "stretch"},
    {LV_GRID_ALIGN_SPACE_EVENLY,   "space_evenly"},
    {LV_GRID_ALIGN_SPACE_BETWEEN,  "space_between"},
    {LV_GRID_ALIGN_SPACE_AROUND,   "space_around"}
};

// ==========================================
// CATEGORIE 3: WIDGET SPECIFIEKE ENUMS
// ==========================================

// 11. Label Long Mode (Tekstgedrag bij overloop)
const EnumMapping array_long_mode[] = {
    {LV_LABEL_LONG_MODE_WRAP,            "wrap"},
    {int(LV_LABEL_LONG_MODE_DOT),        "dot"}, 
    {LV_LABEL_LONG_MODE_SCROLL,          "scroll"},
    {LV_LABEL_LONG_MODE_SCROLL_CIRCULAR, "circular"},
    {LV_LABEL_LONG_MODE_CLIP,            "clip"}
};

// 12. Bar Modus (Vooruitgangsbalk)
const EnumMapping array_bar_mode[] = {
    {LV_BAR_MODE_NORMAL,      "normal"},
    {LV_BAR_MODE_SYMMETRICAL, "symmetrical"},
    {LV_BAR_MODE_RANGE,       "range"}
};

// 13. Slider Oriëntatie
const EnumMapping array_slider_orientation[] = {
    {LV_SLIDER_ORIENTATION_AUTO, "auto"},
    {LV_SLIDER_ORIENTATION_HOR,  "horizontal"},
    {LV_SLIDER_ORIENTATION_VER,  "vertical"}
};

// 14. Image Inner Alignment (V9 specifieke image modi)
const EnumMapping array_image_align[] = {
    {LV_IMAGE_ALIGN_DEFAULT,  "default"},
    {LV_IMAGE_ALIGN_CENTER,   "center"},
    {LV_IMAGE_ALIGN_TILE,     "tile"}
};

// 15. Arc Modus (Cirkelvormige balk)
const EnumMapping array_arc_mode[] = {
    {LV_ARC_MODE_NORMAL,      "normal"},
    {LV_ARC_MODE_SYMMETRICAL, "symmetrical"},
    {LV_ARC_MODE_REVERSE,     "reverse"}
};
