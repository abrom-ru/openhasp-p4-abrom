/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (3a-refactor): HASP_EVENT_* enum copied verbatim from
 * src/hasp/hasp_dispatch.h:31; handler declarations mirror src/hasp/hasp_event.h
 * with LVGL 9 signature (single `lv_event_t*` instead of `(lv_obj_t*, lv_event_t)`).
 */

#ifndef HASP_EVENT_H
#define HASP_EVENT_H

#include "lvgl.h"

/* HASP normalized event IDs — copied verbatim from src/hasp/hasp_dispatch.h:31 */
enum hasp_event_t {
    HASP_EVENT_OFF     = 0,
    HASP_EVENT_ON      = 1,
    HASP_EVENT_UP      = 2,
    HASP_EVENT_DOWN    = 3,
    HASP_EVENT_RELEASE = 4,
    HASP_EVENT_HOLD    = 5,
    HASP_EVENT_LONG    = 6,
    HASP_EVENT_LOST    = 7,
    HASP_EVENT_CHANGED = 32,
};

/* Object event Handlers — LVGL 9 signature.
 * In S3 the signature was `(lv_obj_t* obj, lv_event_t event)`. LVGL 9 passes
 * a single `lv_event_t*` and the handler extracts target/code via getters.
 */
void delete_event_handler(lv_event_t* e);
void generic_event_handler(lv_event_t* e);
void toggle_event_handler(lv_event_t* e);
void slider_event_handler(lv_event_t* e);         /* slider / arc / spinbox */
void selector_event_handler(lv_event_t* e);       /* dropdown / roller / tabview / table */
void btnmatrix_event_handler(lv_event_t* e);      /* buttonmatrix */
void textarea_event_handler(lv_event_t* e);       /* textarea */
void msgbox_event_handler(lv_event_t* e);         /* 3h-4 batch 2: msgbox */
void calendar_event_handler(lv_event_t* e);       /* 3h-4 batch 2: calendar */

#endif
