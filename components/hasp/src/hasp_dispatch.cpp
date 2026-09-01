/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (step 3d MVP): tiny slice of S3 src/hasp/hasp_dispatch.cpp
 * (1798 lines). We port only what's needed to accept text commands and route
 * them to haspPages / hasp_process_attribute / hasp_new_object.
 *
 * Structure mirrors S3 flow but flattened:
 *   hasp_dispatch_command()          — S3 dispatch_simple_text_command (line 382)
 *     ├─ '{' → hasp_dispatch_jsonl()
 *     ├─ '[' → dispatch_json_array()
 *     └─ else → split "topic{=| }payload", route:
 *          ├─ dispatch_parse_button_attribute(topic, payload, update) — S3 line 153
 *          ├─ "page"      → hasp_dispatch_page(payload)               — S3 line 1063
 *          ├─ "clearpage" → hasp_dispatch_clear_page(payload)         — S3 line 1104
 *          └─ else        → log warning ("command not found")
 *
 * MVP omissions (documented in memory/project_hasp_step3d_plan.md):
 *   - dispatch_topic_payload prefix handling (command/, config/, custom/) — MQTT is 4.
 *   - transition/anim/time/delay parameters for page — 3f/3g.
 *   - commands[] table — direct if/else while command count is small.
 *   - dispatch_config, moodlight, screenshot, statusupdate, discovery,
 *     reboot, calibrate, sleep, wakeup, antiburn, run_script, fs, shell,
 *     theme, service — deferred (see plan doc).
 */

#include "hasp_dispatch.h"
#include "hasp.hpp"
#include "hasp_object.h"
#include "hasp_page.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <ArduinoJson.h>
#include "esp_log.h"

static const char* TAG = "hasp_disp";

/* ==================== helpers ==================== */

static inline void trim_leading_ws(const char*& p)
{
    while (*p == ' ' || *p == '\t') p++;
}

/* Parse "p<pageid>b<id>.<attr>" prefix. Returns true on match and fills
 * pageid/objid + advances *attr_out past the '.'. Mirrors S3 dispatch_parse_button_attribute
 * (hasp_dispatch.cpp:153) with the optional-brackets syntax preserved. */
static bool parse_button_attribute(const char* topic, uint8_t* pageid, uint8_t* objid, const char** attr_out)
{
    if (*topic != 'p' && *topic != 'P') return false;
    topic++;

    char* end = nullptr;
    long num;
    if (*topic == '[') {
        topic++;
        num = strtol(topic, &end, 10);
        if (*end != ']') return false;
        end++;
    } else {
        num = strtol(topic, &end, 10);
    }
    if (num < 0 || num > HASP_NUM_PAGES) return false;
    *pageid = (uint8_t)num;
    topic   = end;

    if (*topic == '.') topic++;

    if (*topic != 'b' && *topic != 'B') return false;
    topic++;

    if (*topic == '[') {
        topic++;
        num = strtol(topic, &end, 10);
        if (*end != ']') return false;
        end++;
    } else {
        num = strtol(topic, &end, 10);
    }
    if (num < 0 || num > 255) return false;
    *objid = (uint8_t)num;
    topic  = end;

    if (*topic != '.') return false;
    topic++;

    *attr_out = topic;
    return true;
}

/* ==================== page command (S3 line 1063) ==================== */

void hasp_dispatch_page(const char* payload)
{
    if (!payload || payload[0] == '\0') {
        ESP_LOGI(TAG, "current page = %u", hasp_get_page());
        return;
    }

    if (!strcasecmp(payload, "next")) { haspPages.next(); return; }
    if (!strcasecmp(payload, "prev")) { haspPages.prev(); return; }
    if (!strcasecmp(payload, "back")) { haspPages.back(); return; }

    uint8_t pageid = (uint8_t)atoi(payload);
    hasp_set_page(pageid);
}

/* ==================== clearpage command (S3 line 1104) ==================== */

void hasp_dispatch_clear_page(const char* payload)
{
    if (payload && !strcasecmp(payload, "all")) {
        for (uint8_t pageid = 1; pageid <= HASP_NUM_PAGES; pageid++) {
            haspPages.clear(pageid);
        }
        return;
    }

    uint8_t pageid;
    if (!payload || payload[0] == '\0') {
        pageid = haspPages.get();
    } else {
        pageid = (uint8_t)atoi(payload);
    }
    haspPages.clear(pageid);
}

/* ==================== json array (S3 line 700) ==================== */

static void dispatch_json_array(const char* payload)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        ESP_LOGW(TAG, "json array parse failed: %s", err.c_str());
        return;
    }
    if (!doc.is<JsonArray>()) {
        ESP_LOGW(TAG, "expected json array, got other");
        return;
    }
    /* Each element = one text command or one jsonl object. Recurse. */
    for (JsonVariantConst cmd : doc.as<JsonArrayConst>()) {
        if (cmd.is<const char*>()) {
            hasp_dispatch_command(cmd.as<const char*>());
        } else if (cmd.is<JsonObjectConst>()) {
            /* Serialize back to string and pass to jsonl (mirrors S3 dispatch_json_variant
             * which just calls hasp_new_object; simpler for us to reuse hasp_dispatch_jsonl). */
            char buf[512];
            size_t n = serializeJson(cmd, buf, sizeof(buf));
            if (n > 0 && n < sizeof(buf)) hasp_dispatch_jsonl(buf);
        }
    }
}

/* ==================== main entry (S3 dispatch_simple_text_command line 382) ==================== */

void hasp_dispatch_command(const char* line)
{
    if (!line) return;

    trim_leading_ws(line);

    /* Comments and empties. */
    if (line[0] == '\0') return;
    if (line[0] == '#') return;
    if (line[0] == '/' && line[1] == '/') return;

    /* JSONL object. */
    if (line[0] == '{') {
        hasp_dispatch_jsonl(line);
        return;
    }

    /* JSON array. */
    if (line[0] == '[') {
        dispatch_json_array(line);
        return;
    }

    /* Split on first '=' or ' ' — same rule as S3 line 404-420. '=' wins over ' '. */
    const char* p_eq = strchr(line, '=');
    const char* p_sp = strchr(line, ' ');
    const char* sep  = nullptr;
    bool update      = false;

    if (p_eq) {
        sep    = (p_sp && p_sp < p_eq) ? p_sp : p_eq;
        update = (sep == p_eq);
    } else if (p_sp) {
        sep = p_sp;
    }

    char topic[64];
    const char* payload;
    if (sep) {
        size_t topic_len = (size_t)(sep - line);
        if (topic_len >= sizeof(topic)) topic_len = sizeof(topic) - 1;
        memcpy(topic, line, topic_len);
        topic[topic_len] = '\0';
        payload = sep + 1;
        if (strlen(payload) > 0) update = true; /* space with payload also counts */
    } else {
        strncpy(topic, line, sizeof(topic) - 1);
        topic[sizeof(topic) - 1] = '\0';
        payload = "";
    }

    /* Try pXbY.attr first (S3 line 328 — matched before commands[] for speed). */
    uint8_t pageid, objid;
    const char* attr;
    if (parse_button_attribute(topic, &pageid, &objid, &attr)) {
        hasp_process_attribute(pageid, objid, attr, payload, update);
        return;
    }

    /* MVP command table (flat if-else). Real S3 uses commands[] array — port
     * when >5 commands. */
    if (!strcasecmp(topic, "page")) {
        hasp_dispatch_page(payload);
        return;
    }
    if (!strcasecmp(topic, "clearpage")) {
        hasp_dispatch_clear_page(payload);
        return;
    }
    if (!strcasecmp(topic, "jsonl")) {
        hasp_dispatch_jsonl(payload);
        return;
    }
    if (!strcasecmp(topic, "json")) {
        dispatch_json_array(payload);
        return;
    }

    ESP_LOGW(TAG, "command not found: %s => %s", topic, payload);
}
