/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (step 3b): mirrors src/hasp/hasp_page.h.
 * API preserved (get_obj/get_id/get/get_next/get_prev/get_back/get_name +
 * setters), LVGL 7/8 calls swapped for LVGL 9 equivalents in the .cpp.
 * Filesystem-backed load_jsonl() is out-of-scope for 3b — comes later with FS.
 */

#ifndef HASP_PAGE_H
#define HASP_PAGE_H

#include <stdint.h>
#include "lvgl.h"

#ifndef HASP_NUM_PAGES
#define HASP_NUM_PAGES 12
#endif

#define PAGE_START_INDEX 1 // Page number of array index 0

struct hasp_page_meta_data_t {
    uint8_t prev : 4;
    uint8_t next : 4;
    uint8_t back : 4;
};

namespace hasp {

class Page {
  private:
    char* _pagenames[HASP_NUM_PAGES + 1];             // index 0 = Page 0
    hasp_page_meta_data_t _meta_data[HASP_NUM_PAGES]; // index 0 = Page 1 etc.
    lv_obj_t* _pages[HASP_NUM_PAGES];                 // index 0 = Page 1 etc.
    uint8_t _current_page;

  public:
    Page();
    uint8_t count();
    void init(uint8_t start_page);
    void clear(uint8_t pageid);
    void swap(lv_obj_t* page, uint8_t id);

    /* 3b: plain set without anim wrapper. anim variant lands with dispatch (3d). */
    void set(uint8_t pageid);

    void next();
    void prev();
    void back();

    uint8_t get_next(uint8_t pageid);
    uint8_t get_prev(uint8_t pageid);
    uint8_t get_back(uint8_t pageid);
    char* get_name(uint8_t pageid);

    void set_next(uint8_t pageid, uint8_t nextid);
    void set_prev(uint8_t pageid, uint8_t previd);
    void set_back(uint8_t pageid, uint8_t backid);
    void set_name(uint8_t pageid, const char* name);

    uint8_t get();
    lv_obj_t* get_obj(uint8_t pageid);
    bool get_id(const lv_obj_t* obj, uint8_t* pageid);
    bool is_valid(uint8_t pageid);
};

} // namespace hasp

using hasp::Page;
extern hasp::Page haspPages;

#ifdef __cplusplus
extern "C" {
#endif

/* C entry points for main.cpp / future dispatch. */
void hasp_pages_init(uint8_t start_page);
void hasp_set_page(uint8_t pageid);
uint8_t hasp_get_page(void);

#ifdef __cplusplus
}
#endif

#endif // HASP_PAGE_H
