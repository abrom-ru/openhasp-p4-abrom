/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (step 3e): mirrors src/hasp/hasp_attribute.h.
 *
 * Contents kept 1-to-1 with S3:
 *  - hasp_attribute_type_t enum (lines 43..64)
 *  - ATTR_* SDBM hashes (lines 280..510)
 *  - LV_HASP_PART_* constants (lines 512..521)
 *
 * NOT ported (deferred / not applicable):
 *  - _HASP_ATTRIBUTE(...) macro table (LVGL 7 local-style getters — LVGL 9 has
 *    a different property system; per-obj setters called directly instead).
 *  - hasp_attr_*_const_t / hasp_attr_update_*_t typed tables — replaced by direct
 *    per-obj-type switches in .cpp for now. Will re-introduce in 3f if we need
 *    the compact dispatch density (>50 obj/attr pairs).
 *  - my_obj_set_tag/action/swipe + user-data typed accessors — needs hasp_event
 *    (3f) and full user_data (later).
 */

#ifndef HASP_ATTRIBUTE_H
#define HASP_ATTRIBUTE_H

#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Set/get an attribute of an object.
 * Signature mirrors S3 hasp_attribute.cpp:2657 exactly.
 *   update = true  → apply payload to obj
 *   update = false → get: reply is logged (real MQTT publish arrives in 3f)
 */
void hasp_process_obj_attribute(lv_obj_t* obj, const char* attr_p, const char* payload, bool update);

#ifdef __cplusplus
} /* extern "C" */
#endif

typedef enum {
    HASP_ATTR_TYPE_LONG_MODE_INVALID       = -10,
    HASP_ATTR_TYPE_RANGE_ERROR             = -9,
    HASP_ATTR_TYPE_METHOD_INVALID_FOR_PAGE = -8,
    HASP_ATTR_TYPE_ALIGN_INVALID           = -7,
    HASP_ATTR_TYPE_COLOR_INVALID           = -6,
    HASP_ATTR_TYPE_JSON_INVALID            = -5,
    HASP_ATTR_TYPE_JSON_READONLY           = -4,
    HASP_ATTR_TYPE_STR_READONLY            = -3,
    HASP_ATTR_TYPE_BOOL_READONLY           = -2,
    HASP_ATTR_TYPE_INT_READONLY            = -1,
    HASP_ATTR_TYPE_NOT_FOUND               = 0,
    HASP_ATTR_TYPE_INT,
    HASP_ATTR_TYPE_BOOL,
    HASP_ATTR_TYPE_STR,
    HASP_ATTR_TYPE_JSON,
    HASP_ATTR_TYPE_COLOR,
    HASP_ATTR_TYPE_ALIGN,
    HASP_ATTR_TYPE_DIRECTION_XY,
    HASP_ATTR_TYPE_DIRECTION_CLOCK,
    HASP_ATTR_TYPE_METHOD_OK,
} hasp_attribute_type_t;

/* ==================== SDBM attribute hashes — verbatim from S3 ==================== */

/* Object Part Attributes */
#define ATTR_SIZE 16417
#define ATTR_RADIUS 20786
#define ATTR_CLIP_CORNER 9188
#define ATTR_OPA_SCALE 64875
#define ATTR_TRANSFORM_HEIGHT 55994
#define ATTR_TRANSFORM_WIDTH 48627

/* Background Attributes */
#define ATTR_BG_OPA 48966
#define ATTR_BG_COLOR 64969
#define ATTR_BG_GRAD_DIR 41782
#define ATTR_BG_GRAD_STOP 4025
#define ATTR_BG_MAIN_STOP 63118
#define ATTR_BG_BLEND_MODE 31147
#define ATTR_BG_GRAD_COLOR 44140

/* Margin Attributes */
#define ATTR_MARGIN_TOP 7812
#define ATTR_MARGIN_LEFT 24440
#define ATTR_MARGIN_BOTTOM 37692
#define ATTR_MARGIN_RIGHT 2187

/* Padding Attributes */
#define ATTR_PAD_TOP 59081
#define ATTR_PAD_LEFT 43123
#define ATTR_PAD_INNER 9930
#define ATTR_PAD_RIGHT 65104
#define ATTR_PAD_BOTTOM 3767

/* Text Attributes */
#define ATTR_TEXT_OPA 37166
#define ATTR_TEXT_FONT 22465
#define ATTR_TEXT_COLOR 23473
#define ATTR_TEXT_DECOR 1971
#define ATTR_TEXT_LETTER_SPACE 62079
#define ATTR_TEXT_SEL_COLOR 32076
#define ATTR_TEXT_LINE_SPACE 54829
#define ATTR_TEXT_BLEND_MODE 32195

/* Border Attributes */
#define ATTR_BORDER_OPA 2061
#define ATTR_BORDER_SIDE 53962
#define ATTR_BORDER_POST 49491
#define ATTR_BORDER_BLEND_MODE 23844
#define ATTR_BORDER_WIDTH 24531
#define ATTR_BORDER_COLOR 21264

/* Outline Attributes */
#define ATTR_OUTLINE_OPA 23011
#define ATTR_OUTLINE_PAD 26038
#define ATTR_OUTLINE_COLOR 6630
#define ATTR_OUTLINE_BLEND_MODE 25038
#define ATTR_OUTLINE_WIDTH 9897

/* Shadow Attributes */
#define ATTR_SHADOW_OPA 38401
#define ATTR_SHADOW_WIDTH 13255
#define ATTR_SHADOW_OFS_X 44278
#define ATTR_SHADOW_OFS_Y 44279
#define ATTR_SHADOW_SPREAD 21138
#define ATTR_SHADOW_BLEND_MODE 64048
#define ATTR_SHADOW_COLOR 9988

/* Line Attributes */
#define ATTR_LINE_OPA 24501
#define ATTR_LINE_WIDTH 25467
#define ATTR_LINE_COLOR 22200
#define ATTR_LINE_DASH_WIDTH 32676
#define ATTR_LINE_ROUNDED 15042
#define ATTR_LINE_DASH_GAP 49332
#define ATTR_LINE_BLEND_MODE 60284

/* Value Attributes */
#define ATTR_VALUE_OPA 50482
#define ATTR_VALUE_STR 1091
#define ATTR_VALUE_FONT 9405
#define ATTR_VALUE_ALIGN 27895
#define ATTR_VALUE_COLOR 52661
#define ATTR_VALUE_OFS_X 21415
#define ATTR_VALUE_OFS_Y 21416
#define ATTR_VALUE_LINE_SPACE 26921
#define ATTR_VALUE_BLEND_MODE 4287
#define ATTR_VALUE_LETTER_SPACE 51067

/* Pattern attributes */
#define ATTR_PATTERN_BLEND_MODE 43456
#define ATTR_PATTERN_RECOLOR_OPA 35074
#define ATTR_PATTERN_RECOLOR 7745
#define ATTR_PATTERN_REPEAT 31338
#define ATTR_PATTERN_OPA 43633
#define ATTR_PATTERN_IMAGE 61292

#define ATTR_TRANSITION_PROP_1 49343
#define ATTR_TRANSITION_PROP_2 49344
#define ATTR_TRANSITION_PROP_3 49345
#define ATTR_TRANSITION_PROP_4 49346
#define ATTR_TRANSITION_PROP_5 49347
#define ATTR_TRANSITION_PROP_6 49348
#define ATTR_TRANSITION_TIME 26263
#define ATTR_TRANSITION_PATH 43343
#define ATTR_TRANSITION_DELAY 64537

#define ATTR_IMAGE_OPA 58140
#define ATTR_IMAGE_RECOLOR 52204
#define ATTR_IMAGE_BLEND_MODE 11349
#define ATTR_IMAGE_RECOLOR_OPA 43949

#define ATTR_SCALE_END_LINE_WIDTH 30324
#define ATTR_SCALE_END_BORDER_WIDTH 34380
#define ATTR_SCALE_BORDER_WIDTH 2440
#define ATTR_SCALE_GRAD_COLOR 47239
#define ATTR_SCALE_WIDTH 36017
#define ATTR_SCALE_END_COLOR 44074

/* Page Attributes */
#define ATTR_NEXT 60915
#define ATTR_PREV 21587
#define ATTR_BACK 57799
#define ATTR_NAME 44331

/* Object Attributes */
#define ATTR_X 120
#define ATTR_Y 121
#define ATTR_W 119
#define ATTR_H 104
#define ATTR_OPTIONS 29886
#define ATTR_ENABLED 28193
#define ATTR_CLICK 17064
#define ATTR_OPACITY 10155
#define ATTR_TOGGLE 38580
#define ATTR_HIDDEN 11082
#define ATTR_VIS 16320
#define ATTR_SWIPE 11802
#define ATTR_MODE 45891
#define ATTR_ALIGN 34213
#define ATTR_ROWS 52153
#define ATTR_COLS 36307
#define ATTR_MIN 46130
#define ATTR_MAX 45636
#define ATTR_VAL 15809
#define ATTR_COLOR 58979
#define ATTR_TXT 9328
#define ATTR_TEXT 53869
#define ATTR_TEMPLATE 43290
#define ATTR_SRC 4964
#define ATTR_ID 6715
#define ATTR_EXT_CLICK_H 46643
#define ATTR_EXT_CLICK_V 46657
#define ATTR_ANIM_TIME 59451
#define ATTR_ANIM_SPEED 281
#define ATTR_START_VALUE 11828
#define ATTR_COMMENT 62559
#define ATTR_TAG 7866
#define ATTR_JSONL 61604
#define ATTR_MODE_FIXED 35736

// methods
#define ATTR_DELETE 50027
#define ATTR_CLEAR 1069
#define ATTR_TO_FRONT 44741
#define ATTR_TO_BACK 24555

// Gauge
#define ATTR_CRITICAL_VALUE 39281
#define ATTR_ANGLE 2387
#define ATTR_LABEL_COUNT 20356
#define ATTR_LINE_COUNT 57860
#define ATTR_FORMAT 38871

// Arc
#define ATTR_TYPE 1658
#define ATTR_ROTATION 44830
#define ATTR_ADJUSTABLE 19145
#define ATTR_START_ANGLE 44310
#define ATTR_END_ANGLE 41103
#define ATTR_START_ANGLE1 39067
#define ATTR_END_ANGLE1 33634

// Dropdown
#define ATTR_DIRECTION 32415
#define ATTR_SYMBOL 33592
#define ATTR_OPEN 25738
#define ATTR_CLOSE 41880
#define ATTR_MAX_HEIGHT 30946
#define ATTR_SHOW_SELECTED 56029

// Buttonmatrix
#define ATTR_ONE_CHECK 45935

// Tabview
#define ATTR_BTN_POS 35697
#define ATTR_COUNT 29103

// Msgbox
#define ATTR_MODAL 7405
#define ATTR_AUTO_CLOSE 7880

// Image
#define ATTR_OFFSET_X 65388
#define ATTR_OFFSET_Y 65389
#define ATTR_PIVOT_X 42715
#define ATTR_PIVOT_Y 42716
#define ATTR_ZOOM 20403
#define ATTR_AUTO_SIZE 63729
#define ATTR_ANTIALIAS 55278

// Spinner
#define ATTR_SPEED 14375
#define ATTR_THICKNESS 24180

// Line
#define ATTR_POINTS 8643
#define ATTR_Y_INVERT 44252

/* hasp user data */
#define ATTR_ACTION 42102
#define ATTR_TRANSITION 10933
#define ATTR_GROUPID 48986
#define ATTR_OBJID 41010
#define ATTR_OBJ 53623

#define ATTR_TEXT_MAC 38107
#define ATTR_TEXT_IP 41785
#define ATTR_TEXT_HOSTNAME 10125
#define ATTR_TEXT_MODEL 54561
#define ATTR_TEXT_VERSION 60178
#define ATTR_TEXT_SSID 62981

/* LVGL 7 parts — kept for source-compat with S3 attribute code paths.
 * Under LVGL 9 all setters take LV_PART_* directly; these values map
 * MAIN=0 conveniently, others are informational until specific-part
 * attributes are wired in 3g. */
#define LV_HASP_PART_MAIN 0
#define LV_HASP_PART_INDICATOR 10
#define LV_HASP_PART_KNOB 20
#define LV_HASP_PART_ITEMS_BG 30
#define LV_HASP_PART_ITEMS 40
#define LV_HASP_PART_SELECTED 50
#define LV_HASP_PART_TICKS 60
#define LV_HASP_PART_CURSOR 70
#define LV_HASP_PART_SCROLLBAR 80
#define LV_HASP_PART_SPECIAL 90

#endif /* HASP_ATTRIBUTE_H */
