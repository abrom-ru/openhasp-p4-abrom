/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (step 3b): mirrors src/hasp/hasp_page.cpp.
 *
 * LVGL 7/8 → LVGL 9 adaptations documented per site:
 *   - lv_scr_act() → lv_screen_active()
 *   - lv_scr_load_anim() → lv_screen_load() (no anim wrapper in 3b — anim lives in
 *     dispatch layer via my_scr_load_anim, which is ported in 3d)
 *   - lv_obj_create(NULL, NULL) → lv_obj_create(NULL) (parent==NULL creates a screen)
 *   - lv_obj_set_event_cb → lv_obj_add_event_cb(..., LV_EVENT_ALL, NULL)
 *   - lv_obj_add_protect(LV_PROTECT_PRESS_LOST) — default behavior in LVGL 9, dropped
 *   - user_data bit-field → allocated hasp_obj_user_data_t* + lv_obj_set_user_data
 *   - hasp_calloc/hasp_free → calloc/free (no PSRAM allocator wrapper in mvp)
 *
 * dispatch_current_page() is a stub — real MQTT/log dispatch appears in 3d/3f.
 */

#include "hasp_page.h"
#include "hasp_object.h"
#include "hasp_event.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"

static const char* TAG = "hasp_page";

/* Stub for dispatch_current_page() — S3 publishes the current page over MQTT
 * and calls object_dispatch_state on the screen. Lands with 3d/3f. */
static void dispatch_current_page(uint8_t pageid)
{
    ESP_LOGI(TAG, "current page = %u", pageid);
}

namespace hasp {

Page::Page()
{
    // LVGL is not yet initialized at construction time.
    _current_page = PAGE_START_INDEX;
    for (int i = 0; i < HASP_NUM_PAGES + 1; i++) _pagenames[i] = nullptr;
    for (int i = 0; i < HASP_NUM_PAGES; i++) _pages[i] = nullptr;
}

bool Page::is_valid(uint8_t pageid)
{
    if (pageid > 0 && pageid <= HASP_NUM_PAGES) return true;
    ESP_LOGW(TAG, "invalid page %u", pageid);
    return false;
}

uint8_t Page::count()
{
    return (uint8_t)(sizeof(_pages) / sizeof(*_pages));
}

/* Mirrors src/hasp/hasp_page.cpp:37 Page::swap().
 * LVGL 9 adaptations: user_data.objid → hasp_obj_user_data_t*; LV_PROTECT_PRESS_LOST
 * is default; lv_obj_set_event_cb → lv_obj_add_event_cb; lv_scr_act → lv_screen_active;
 * lv_scr_load_anim → lv_screen_load (no anim in 3b — anim helper is dispatch layer). */
void Page::swap(lv_obj_t* page, uint8_t id)
{
    if (id >= count()) {
        ESP_LOGW(TAG, "swap: invalid page id %u", id);
        return;
    }

    lv_obj_t* prev_page_obj = _pages[id];
    _pages[id] = page;

    hasp_obj_user_data_t* ud = (hasp_obj_user_data_t*)calloc(1, sizeof(*ud));
    if (ud) {
        ud->objid = LV_HASP_SCREEN;
        ud->id    = 0;
        lv_obj_set_user_data(page, ud);
    }
    lv_obj_add_event_cb(page, generic_event_handler, LV_EVENT_ALL, nullptr);

    if (prev_page_obj) {
        if (prev_page_obj == lv_screen_active()) {
            lv_screen_load(page);
            lv_obj_delete_async(prev_page_obj);
        } else {
            lv_obj_delete(prev_page_obj);
        }
    }
}

void Page::init(uint8_t start_page)
{
    lv_obj_clean(lv_layer_top());

    for (int i = 0; i < count(); i++) {
        /* LVGL 9: lv_obj_create(NULL) creates a screen (parent == NULL). */
        lv_obj_t* page = lv_obj_create(NULL);
        Page::swap(page, i);

        uint16_t thispage  = i + PAGE_START_INDEX;
        _meta_data[i].prev = thispage == PAGE_START_INDEX ? HASP_NUM_PAGES : thispage - PAGE_START_INDEX;
        _meta_data[i].next = thispage == HASP_NUM_PAGES ? PAGE_START_INDEX : thispage + PAGE_START_INDEX;
        _meta_data[i].back = start_page;

        set_name(i + PAGE_START_INDEX, nullptr);
    }

    /* Load the start page so subsequent lv_screen_active() returns our page 1
     * (S3 does this via dispatch/set from a later call site — we do it here since
     * dispatch isn't wired yet in 3b). */
    if (is_valid(start_page)) {
        lv_screen_load(_pages[start_page - PAGE_START_INDEX]);
        _current_page = start_page;
    }
}

/* Mirrors src/hasp/hasp_page.cpp:82 Page::clear() — used by `clearpage N` dispatch (3d). */
void Page::clear(uint8_t pageid)
{
    lv_obj_t* page = get_obj(pageid);
    if (page == lv_layer_top() || is_valid(pageid)) {
        ESP_LOGI(TAG, "clear page %u", pageid);
        lv_obj_clean(page);
    } else {
        ESP_LOGW(TAG, "clear: invalid page/layer %u", pageid);
    }
}

void Page::set(uint8_t pageid)
{
    if (!is_valid(pageid)) return;

    lv_obj_t* page = get_obj(pageid);
    if (!page) {
        ESP_LOGW(TAG, "invalid page %u (no obj)", pageid);
        return;
    }

    if (page == lv_screen_active()) {
        _current_page = pageid;
        dispatch_current_page(pageid);
        return;
    }

    ESP_LOGI(TAG, "change page -> %u", pageid);
    lv_screen_load(page);
    _current_page = pageid;
    dispatch_current_page(pageid);
}

uint8_t Page::get_next(uint8_t pageid)
{
    return is_valid(pageid) ? _meta_data[pageid - PAGE_START_INDEX].next : 0;
}

uint8_t Page::get_prev(uint8_t pageid)
{
    return is_valid(pageid) ? _meta_data[pageid - PAGE_START_INDEX].prev : 0;
}

uint8_t Page::get_back(uint8_t pageid)
{
    return is_valid(pageid) ? _meta_data[pageid - PAGE_START_INDEX].back : 0;
}

char* Page::get_name(uint8_t pageid)
{
    return pageid <= HASP_NUM_PAGES ? _pagenames[pageid] : nullptr;
}

void Page::set_next(uint8_t pageid, uint8_t nextid)
{
    if (is_valid(pageid) && is_valid(nextid)) _meta_data[pageid - PAGE_START_INDEX].next = nextid;
}

void Page::set_prev(uint8_t pageid, uint8_t previd)
{
    if (is_valid(pageid) && is_valid(previd)) _meta_data[pageid - PAGE_START_INDEX].prev = previd;
}

void Page::set_back(uint8_t pageid, uint8_t backid)
{
    if (is_valid(pageid) && is_valid(backid)) _meta_data[pageid - PAGE_START_INDEX].back = backid;
}

void Page::set_name(uint8_t pageid, const char* name)
{
    if (pageid > HASP_NUM_PAGES) {
        ESP_LOGW(TAG, "set_name: invalid page %u", pageid);
        return;
    }

    if (_pagenames[pageid]) {
        free(_pagenames[pageid]);
        _pagenames[pageid] = nullptr;
    }

    if (!name) return;
    size_t size = strlen(name) + 1;
    if (size > 1) {
        _pagenames[pageid] = (char*)calloc(size, sizeof(char));
        if (!_pagenames[pageid]) return;
        strncpy(_pagenames[pageid], name, size);
    }
}

void Page::next()
{
    set(_meta_data[_current_page - PAGE_START_INDEX].next);
}

void Page::prev()
{
    set(_meta_data[_current_page - PAGE_START_INDEX].prev);
}

void Page::back()
{
    set(_meta_data[_current_page - PAGE_START_INDEX].back);
}

uint8_t Page::get()
{
    return _current_page;
}

lv_obj_t* Page::get_obj(uint8_t pageid)
{
    if (pageid == 0) return lv_layer_top();
    if (pageid == 255) return lv_layer_sys();
    if (pageid > count()) return nullptr;
    return _pages[pageid - PAGE_START_INDEX];
}

bool Page::get_id(const lv_obj_t* obj, uint8_t* pageid)
{
    lv_obj_t* page = lv_obj_get_screen(const_cast<lv_obj_t*>(obj));
    if (!page) return false;

    if (page == lv_layer_top()) { *pageid = 0;   return true; }
    if (page == lv_layer_sys()) { *pageid = 255; return true; }

    for (uint8_t i = 0; i < count(); i++) {
        if (page == _pages[i]) {
            *pageid = i + PAGE_START_INDEX;
            return true;
        }
    }
    return false;
}

} // namespace hasp

hasp::Page haspPages;

extern "C" void hasp_pages_init(uint8_t start_page)
{
    haspPages.init(start_page);
}

extern "C" void hasp_set_page(uint8_t pageid)
{
    haspPages.set(pageid);
}

extern "C" uint8_t hasp_get_page(void)
{
    return haspPages.get();
}
