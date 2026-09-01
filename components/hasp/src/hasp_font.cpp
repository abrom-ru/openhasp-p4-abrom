/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (step 3h-2 stage 3) from src/hasp/hasp_font.cpp.
 * LVGL 9 FreeType API rewrite:
 *   - S3 (LVGL 7): lv_freetype_init(max_faces, max_sizes, max_bytes) +
 *                   lv_ft_info_t{name,weight,mem,mem_size,style} +
 *                   lv_ft_font_init(&info)/lv_ft_font_destroy(font)
 *   - LVGL 9:      lv_freetype_init(max_glyph_cnt) +
 *                   lv_freetype_font_create(path, mode, size, style)/
 *                   lv_freetype_font_delete(font)
 *
 * Memory-blob loading (OPENHASP_TTF_START/END embed) is NOT available in
 * LVGL 9 FreeType — the API is file-only. We rely on the openhasp.ttf that
 * stage 2 baked into the LittleFS 'storage' partition, accessed via the
 * LVGL FS 'L:' drive (CONFIG_LV_FS_STDIO_PATH="/littlefs/").
 *
 * Cache: simple singly linked list keyed by full payload string, matches
 * S3 font_find_in_list/font_add_to_list semantics. lv_ll_t from S3 is
 * replaced by malloc'd nodes to avoid pulling LVGL private APIs.
 */

#include "hasp_font.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include "esp_log.h"

static const char* TAG = "hasp_font";

#if LV_USE_FREETYPE

/* Default TTF path — POSIX path on the LittleFS mount from stage 2.
 * Note: with CONFIG_LV_FREETYPE_USE_LVGL_PORT=n the FreeType port uses the
 * standard POSIX fopen()/stdio backend (not lv_fs_open), so paths must be
 * real VFS paths — the LVGL 'L:' drive-letter mapping does NOT apply here.
 * That mapping is still valid for other LVGL widgets (image, etc.).  */
#define OPENHASP_TTF_PATH "/littlefs/openhasp.ttf"

typedef struct hasp_font_node_s {
    char*                    payload; /* full payload key, e.g. "openhasp24" */
    lv_font_t*               font;    /* FreeType-backed lv_font_t */
    struct hasp_font_node_s* next;
} hasp_font_node_t;

static hasp_font_node_t* s_font_head = NULL;

static bool str_is_only_digits(const char* s)
{
    if (!s || !*s) return false;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
    }
    return true;
}

/* Mirrors S3 font_split_payload: return offset of first digit in payload,
 * or 0 if payload contains no trailing digit run. */
static size_t font_split_payload(const char* payload)
{
    if (!payload) return 0;
    size_t pos = 0;
    while (payload[pos] != '\0') {
        if (str_is_only_digits(payload + pos)) return pos;
        pos++;
    }
    return 0;
}

static lv_font_t* font_find_in_list(const char* payload)
{
    for (hasp_font_node_t* n = s_font_head; n; n = n->next) {
        if (strcmp(n->payload, payload) == 0) return n->font;
    }
    return NULL;
}

static lv_font_t* font_add_to_list(const char* payload)
{
    if (!payload || !*payload) return NULL;

    char     path[128];
    uint16_t size = 0;

    if (str_is_only_digits(payload)) {
        /* Pure numeric payload → default openhasp.ttf at that size. */
        strncpy(path, OPENHASP_TTF_PATH, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        size = (uint16_t)atoi(payload);
    } else {
        size_t pos = font_split_payload(payload);
        if (pos == 0 || pos > 56) {
            ESP_LOGW(TAG, "no size suffix in '%s'", payload);
            return NULL;
        }
        size = (uint16_t)atoi(payload + pos);
        if (pos > 0 && payload[pos - 1] == '_') pos--; /* strip trailing '_' */

        char name[64];
        if (pos >= sizeof(name)) pos = sizeof(name) - 1;
        memset(name, 0, sizeof(name));
        strncpy(name, payload, pos);
        snprintf(path, sizeof(path), "/littlefs/%s.ttf", name);
    }

    if (size < 8) {
        ESP_LOGW(TAG, "size %u too small (min 8)", size);
        return NULL;
    }

    /* Probe file first — clearer log than FreeType's internal miss. Uses
     * POSIX stat (same VFS that FreeType's fopen will hit). */
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGW(TAG, "file not found: %s (errno=%d)", path, errno);
        return NULL;
    }

    lv_font_t* font = lv_freetype_font_create(
        path, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, size, LV_FREETYPE_FONT_STYLE_NORMAL);
    if (!font) {
        ESP_LOGE(TAG, "FreeType create failed for %s size=%u", path, size);
        return NULL;
    }
    ESP_LOGI(TAG, "loaded %s size=%u line_h=%d", path, size, (int)font->line_height);

    hasp_font_node_t* node = (hasp_font_node_t*)calloc(1, sizeof(*node));
    if (!node) {
        lv_freetype_font_delete(font);
        return NULL;
    }
    size_t plen   = strlen(payload);
    node->payload = (char*)malloc(plen + 1);
    if (!node->payload) {
        free(node);
        lv_freetype_font_delete(font);
        return NULL;
    }
    memcpy(node->payload, payload, plen + 1);
    node->font = font;
    node->next = s_font_head;
    s_font_head = node;
    return font;
}

#endif /* LV_USE_FREETYPE */

void font_setup(void)
{
#if LV_USE_FREETYPE
    /* LVGL 9 auto-initialises FreeType from lv_init() when LV_USE_FREETYPE=y
     * — a second lv_freetype_init() then returns LV_RESULT_INVALID with the
     * "freetype already initialized" warning, which is expected and not an
     * error. We call it defensively (no-op if already up) and treat both
     * outcomes as OK. */
    lv_result_t r = lv_freetype_init(CONFIG_LV_FREETYPE_CACHE_FT_GLYPH_CNT);
    ESP_LOGI(TAG, "FreeType ready (glyph cache=%d, init r=%d — LVGL may have inited it earlier)",
             CONFIG_LV_FREETYPE_CACHE_FT_GLYPH_CNT, (int)r);
    s_font_head = NULL;
#else
    ESP_LOGW(TAG, "FreeType disabled at compile time (LV_USE_FREETYPE=0)");
#endif
}

lv_font_t* get_font(const char* payload)
{
#if LV_USE_FREETYPE
    if (!payload || !*payload) return NULL;
    lv_font_t* f = font_find_in_list(payload);
    if (f) return f;
    return font_add_to_list(payload);
#else
    (void)payload;
    return NULL;
#endif
}

void font_clear_list(const char* /*payload*/)
{
#if LV_USE_FREETYPE
    hasp_font_node_t* n = s_font_head;
    while (n) {
        hasp_font_node_t* next = n->next;
        if (n->font) lv_freetype_font_delete(n->font);
        free(n->payload);
        free(n);
        n = next;
    }
    s_font_head = NULL;
#endif
}
