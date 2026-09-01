/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (3a-refactor): enum + HASP_OBJ_* sdbm constants copied
 * verbatim from src/hasp/hasp_object.h. user_data struct adapted for LVGL 9
 * (LVGL 9 user_data is `void*`, so we allocate a small struct on the heap and
 * stash the pointer via lv_obj_set_user_data — same *fields* as S3, different
 * mechanism, freed in delete_event_handler).
 */

#ifndef HASP_OBJECT_H
#define HASP_OBJECT_H

#include <stdint.h>
#include <ArduinoJson.h>
#include "lvgl.h"

/* Field-name constants (same as S3 FP_* PROGMEM strings, without PROGMEM). */
static const char FP_SKIP[]     = "skip";
static const char FP_PAGE[]     = "page";
static const char FP_ID[]       = "id";
static const char FP_OBJ[]      = "obj";
static const char FP_PARENTID[] = "parentid";
static const char FP_GROUPID[]  = "groupid";

/* Per-object HASP metadata. Allocated per-object, pointed to by
 * lv_obj_get_user_data(). Freed in delete_event_handler on LV_EVENT_DELETE.
 * Fields mirror the S3 `lv_obj_user_data_t` bit-field struct.
 *
 * 3h-4: `extra` slot holds widget-specific heap resource that needs
 * lifetime tied to the LVGL object — currently LINE points array
 * (freed unconditionally in delete_event_handler via free()). If a
 * future widget needs a non-free()-able resource, promote to a
 * discriminated struct instead.
 */
typedef struct {
    uint8_t id;
    uint8_t objid;      // lv_hasp_obj_type_t
    uint8_t groupid;
    void*   extra;      // widget-specific heap resource (line points, …)
} hasp_obj_user_data_t;

/* HASP object type ids — copied verbatim from src/hasp/hasp_object.h:41 */
enum lv_hasp_obj_type_t {
    /* Containers */
    LV_HASP_SCREEN    = 1,
    LV_HASP_CONTAINER = 2,
    LV_HASP_WINDOW    = 3,
    LV_HASP_MSGBOX    = 4,
    LV_HASP_TILEVIEW  = 5,
    LV_HASP_TABVIEW   = 6,
    LV_HASP_TAB       = 7,
    LV_HASP_PAGE      = 8,
    LV_HASP_SPAN      = 9,

    /* Controls */
    LV_HASP_KEYBOARD  = 10,
    LV_HASP_OBJECT    = 11,
    LV_HASP_BUTTON    = 12,
    LV_HASP_BTNMATRIX = 13,
    LV_HASP_IMGBTN    = 14,
    LV_HASP_CHECKBOX  = 15,
    LV_HASP_SWITCH    = 16,
    LV_HASP_SLIDER    = 17,
    LV_HASP_TEXTAREA  = 18,
    LV_HASP_SPINBOX   = 19,
    LV_HASP_CPICKER   = 20,

    /* Visualizers */
    LV_HASP_LABEL     = 21,
    LV_HASP_GAUGE     = 22,
    LV_HASP_BAR       = 23,
    LV_HASP_LINEMETER = 24,
    LV_HASP_LED       = 25,
    LV_HASP_ARC       = 26,
    LV_HASP_SPINNER   = 27,
    LV_HASP_CHART     = 28,
    LV_HASP_DATETIME  = 40,

    /* Selectors */
    LV_HASP_DROPDOWN = 29,
    LV_HASP_ROLLER   = 30,
    LV_HASP_LIST     = 31,
    LV_HASP_TABLE    = 32,
    LV_HASP_CALENDER = 33,
    LV_HASP_MENU     = 34,

    /* Graphics */
    LV_HASP_LINE      = 36,
    LV_HASP_IMAGE     = 37,
    LV_HASP_ANIMIMAGE = 38,
    LV_HASP_CANVAS    = 39,
    LV_HASP_MASK      = 40,
    LV_HASP_QRCODE    = 41,

    /* Custom */
    LV_HASP_ALARM = 60,
};

/* SDBM hashes of obj-type strings — copied verbatim from src/hasp/hasp_object.h:157 */
#define HASP_OBJ_BAR       1971
#define HASP_OBJ_BTN       3164
#define HASP_OBJ_CPICKER   3313
#define HASP_OBJ_CHECKBOX  1923
#define HASP_OBJ_SPINNER   7097
#define HASP_OBJ_MSGBOX    7498
#define HASP_OBJ_TABLE     12078
#define HASP_OBJ_ROLLER    13258
#define HASP_OBJ_LABEL     13684
#define HASP_OBJ_KEYBOARD  14343
#define HASP_OBJ_PAGE      19759
#define HASP_OBJ_WIN       20284
#define HASP_OBJ_TEXTAREA  24186
#define HASP_OBJ_IMGBTN    24441
#define HASP_OBJ_SPINBOX   25641
#define HASP_OBJ_CALENDAR  30334
#define HASP_OBJ_IMG       30499
#define HASP_OBJ_QRCODE    50958
#define HASP_OBJ_GAUGE     33145
#define HASP_OBJ_CHART     34654
#define HASP_OBJ_LINE      34804
#define HASP_OBJ_LIST      35134
#define HASP_OBJ_SLIDER    35265
#define HASP_OBJ_CANVAS    35480
#define HASP_OBJ_TILEVIEW  36019
#define HASP_OBJ_CONT      36434
#define HASP_OBJ_SWITCH    38484
#define HASP_OBJ_LED       41899
#define HASP_OBJ_DROPDOWN  49169
#define HASP_OBJ_BTNMATRIX 49629
#define HASP_OBJ_OBJ       53623
#define HASP_OBJ_OBJMASK   55395
#define HASP_OBJ_LMETER    62749
#define HASP_OBJ_LINEMETER 55189
#define HASP_OBJ_TABVIEW   63226
#define HASP_OBJ_TAB       7861
#define HASP_OBJ_ARC       64594
#define HASP_OBJ_ALARM     3153

/* Access helpers — LVGL 9 adaptation of S3 inline getters. */
static inline hasp_obj_user_data_t* hasp_obj_ud(const lv_obj_t* obj)
{
    return obj ? static_cast<hasp_obj_user_data_t*>(lv_obj_get_user_data(const_cast<lv_obj_t*>(obj))) : nullptr;
}

static inline lv_hasp_obj_type_t obj_get_type(const lv_obj_t* obj)
{
    hasp_obj_user_data_t* ud = hasp_obj_ud(obj);
    return ud ? static_cast<lv_hasp_obj_type_t>(ud->objid) : static_cast<lv_hasp_obj_type_t>(0);
}

static inline bool obj_check_type(const lv_obj_t* obj, lv_hasp_obj_type_t haspobjtype)
{
    hasp_obj_user_data_t* ud = hasp_obj_ud(obj);
    return ud && ud->objid == static_cast<uint8_t>(haspobjtype);
}

/* Create a new object described by `config` on the given saved_page_id. */
void hasp_new_object(const JsonObject& config, uint8_t& saved_page_id);

/* hasp_process_attribute — thin wrapper: resolve (pageid, objid) via
 * hasp_find_obj_from_page_id, then call hasp_process_obj_attribute (declared
 * in hasp_attribute.h with C linkage). Mirrors S3 hasp_object.cpp:166.
 * The obj-taking variant lives in hasp_attribute.h — don't re-declare here
 * (a second decl at C++ linkage collides with that extern "C" one). */
void hasp_process_attribute(uint8_t pageid, uint8_t objid, const char* attr, const char* payload, bool update);

/* Object registry (step 3c) — mirrors src/hasp/hasp_object.cpp:22..69.
 * Recursive child walk over the LVGL tree, matching on hasp_obj_user_data_t.id.
 * Returns nullptr if not found (or `parent` when objid == 0, matching S3). */
lv_obj_t* hasp_find_obj_from_parent_id(lv_obj_t* parent, uint8_t objid);
lv_obj_t* hasp_find_obj_from_page_id(uint8_t pageid, uint8_t objid);
bool      hasp_find_id_from_obj(const lv_obj_t* obj, uint8_t* pageid, uint8_t* objid);

/* State topic dispatcher (step 3f) — mirrors src/hasp/hasp_object.cpp:110.
 * Formats `pXbY` (or the named page) and forwards to the state-topic sink.
 * MVP: sink is ESP_LOGI only ("state pXbY => <payload>"); real MQTT publish
 * lands in step 4 alongside dispatch_state_subtopic + hasp_mqtt. */
void object_dispatch_state(uint8_t pageid, uint8_t btnid, const char* payload);

#endif
