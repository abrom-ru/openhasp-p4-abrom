/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* p4-abrom 3a-refactor: hasp.cpp is now a thin entry point matching S3-abrom
 * dispatch flow — parse one JSONL line into a JsonObject and hand it to
 * hasp_new_object() (which does the switch + attribute application).
 * The object switch, event handlers, and hash utilities live in dedicated
 * files (hasp_object / hasp_event / hasp_parser), same slicing as S3.
 */

#include "hasp.hpp"
#include "hasp_object.h"
#include "hasp_page.h"
#include "hasp_font.h"
#include "hasp_theme.h"

#include <ArduinoJson.h>
#include "esp_log.h"

static const char* TAG = "hasp";

extern "C" esp_err_t hasp_init(void)
{
    ESP_LOGI(TAG, "hasp_init (step 3b: haspPages + %d screens)", HASP_NUM_PAGES);
    /* 3h-2 stage 3: init FreeType before pages so labels created with a
     * text_font attribute in early jsonl can resolve the font. */
    font_setup();
    /* 3h-3a: apply theme BEFORE creating page screens — matches S3
     * haspSetup ordering (hasp.cpp:602 hasp_set_theme → hasp_init → pages).
     * If we set the theme after objects exist LVGL 9 still restyles via
     * lv_obj_report_style_change, but doing it first is cleaner and cheaper. */
    hasp_set_theme(haspThemeId);
    /* 3b: allocate the N page screens and load page 1. Caller holds LVGL lock. */
    haspPages.init(PAGE_START_INDEX);
    return ESP_OK;
}

extern "C" esp_err_t hasp_dispatch_jsonl(const char* line)
{
    if (line == nullptr) {
        ESP_LOGE(TAG, "dispatch: null line");
        return ESP_ERR_INVALID_ARG;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        ESP_LOGE(TAG, "dispatch: parse failed: %s", err.c_str());
        return ESP_ERR_INVALID_ARG;
    }

    /* saved_page_id: sticky across jsonl lines so a `{"page":2}` line applies to
     * all following lines without a page field — matches S3 dispatch_parse_jsonl. */
    static uint8_t saved_page_id = PAGE_START_INDEX;
    hasp_new_object(doc.as<JsonObject>(), saved_page_id);
    return ESP_OK;
}
