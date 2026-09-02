#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize HASP core.
 * Must be called under LVGL lock or before LVGL task starts.
 * Currently a no-op — placeholder for future object registry / page system.
 */
esp_err_t hasp_init(void);

/**
 * Parse one JSONL line and materialize a single LVGL object on the active screen.
 * Supported fields (step 3a): page, id, obj, x, y, w, h, text, val.
 * Supported obj types: "btn", "label", "switch", "slider", "checkbox", "bar".
 * Caller must hold the LVGL lock.
 *
 * @param line NUL-terminated JSON object (single line, no trailing newline required).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on parse error / unknown obj type.
 */
esp_err_t hasp_dispatch_jsonl(const char* line);

/* Default autoload path on LittleFS. Mirrors S3 `haspPagesPath` (hasp.cpp:87). */
#define HASP_PAGES_JSONL "/littlefs/pages.jsonl"

/**
 * Load pages/objects from a jsonl file on LittleFS.
 * One `{...}` JSON object per line; `#` and `//` comments and empty lines are
 * skipped. Missing file is not an error (logs a warning, returns ESP_OK) —
 * matches S3 `Page::load_jsonl` behavior (hasp_page.cpp:218-220).
 * Caller must hold the LVGL lock.
 *
 * @param path POSIX path, e.g. "/littlefs/pages.jsonl". NULL = HASP_PAGES_JSONL.
 * @return ESP_OK regardless of missing file; ESP_FAIL only on unrecoverable I/O.
 */
esp_err_t hasp_load_pages_jsonl(const char* path);

/* Page switching (step 3b). Caller must hold the LVGL lock. */
void    hasp_set_page(uint8_t pageid);
uint8_t hasp_get_page(void);

#ifdef __cplusplus
}
#endif
