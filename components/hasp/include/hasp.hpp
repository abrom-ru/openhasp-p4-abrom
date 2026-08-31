#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize HASP core minimum (step 1 scope).
 * Must be called under LVGL lock or before LVGL task starts.
 * Currently a no-op — placeholder for future object registry / page system.
 */
esp_err_t hasp_init(void);

/**
 * Parse one JSONL line and materialize a single LVGL object on the active screen.
 * Supported fields (step 1 minimum): page, id, obj, x, y, w, h, text.
 * Supported obj types: "btn".
 * Caller must hold the LVGL lock.
 *
 * @param line NUL-terminated JSON object (single line, no trailing newline required).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on parse error / unknown obj type.
 */
esp_err_t hasp_dispatch_jsonl(const char* line);

#ifdef __cplusplus
}
#endif
