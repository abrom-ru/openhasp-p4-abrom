/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (step 3h-2 stage 3) from src/hasp/hasp_font.h.
 * Signatures kept 1-to-1 with S3; implementation retargeted to LVGL 9
 * FreeType API in hasp_font.cpp.
 *
 * Payload format (matches S3 get_font):
 *   "<name><size>"     e.g. "openhasp24"  → L:<name>.ttf @ size
 *   "<name>_<size>"    e.g. "openhasp_24" → underscore stripped
 *   "<size>"           e.g. "24"          → default openhasp.ttf @ size
 * The 'L' drive maps to /littlefs/ via CONFIG_LV_FS_STDIO_LETTER=76.
 */

#ifndef HASP_FONT_H
#define HASP_FONT_H

#include "lvgl.h"

void       font_setup(void);
lv_font_t* get_font(const char* payload);
void       font_clear_list(const char* payload);

#endif /* HASP_FONT_H */
