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
#include "hasp_mqtt.hpp"   // step 4b: dispatch_state_subtopic → hasp_mqtt_publish_state

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>           // step 7D-1: strftime for sensors 'time' field

#include <ArduinoJson.h>
#include "esp_log.h"
#include "esp_timer.h"      // step 7B: uptime
#include "esp_heap_caps.h"  // step 7B: free/min heap
#include "esp_netif.h"      // step 7B: ip lookup for statusupdate
#include "esp_wifi.h"       // step 7B: rssi/ssid for statusupdate
#include "esp_idf_version.h"// step 7B: IDF_VER for statusupdate 'version'
#include "lvgl.h"           // step 7B: tftWidth/Height from active display

static const char* TAG = "hasp_disp";

/* ==================== step 7B: telemetry / teleperiod ==================== */
/* S3 hasp_dispatch.cpp:51 — dispatch_setings.teleperiod default 300 s.
 * Kept as file-locals here (dispatch_conf_t was one field only) so we don't
 * inflate the public header. hasp_dispatch_set_teleperiod() is the setter
 * called by MQTT config; hasp_every_second() consumes both. */
static uint16_t s_teleperiod_s               = 300;
static uint16_t s_seconds_to_next_teleperiod = 0;
/* Step 7D-1: sensordata counter. S3 uses a separate
 * dispatchSecondsToNextSensordata (hasp_dispatch.cpp:1339) that decrements
 * in parallel with the teleperiod counter and resets to the same value on
 * publish. Kept as its own file-local so future 7D-2 (discovery) can add a
 * third counter the same way without a struct refactor. */
static uint16_t s_seconds_to_next_sensordata = 0;

void hasp_dispatch_set_teleperiod(uint16_t seconds)
{
    /* 0 disables periodic publish entirely, matching S3 behavior where
     * `teleperiod > 0` gates every branch of dispatchEverySecond(). */
    s_teleperiod_s = seconds;
    /* Reset every tick counter so a config change takes effect immediately —
     * next hasp_every_second() reloads from the new value on the following
     * publish rather than waiting out the old countdown. */
    s_seconds_to_next_teleperiod = seconds;
    s_seconds_to_next_sensordata = seconds;
    ESP_LOGI(TAG, "teleperiod set to %u s", (unsigned)seconds);
}

/* ==================== state subtopic sink (step 4b) ==================== */
/* Mirrors S3 hasp_dispatch.cpp:64 dispatch_state_subtopic. In S3 this is the
 * one seam through which every state-side JSON payload leaves the device
 * (objects, page changes, dim, moodlight, statusupdate, config, sensors...).
 * Step 4b wires only the MQTT path; step 4c will fan out to group/broadcast. */
void dispatch_state_subtopic(const char* subtopic, const char* payload)
{
    if (!subtopic || !payload) return;

    int rc = hasp_mqtt_publish_state(subtopic, payload);
    if (rc == ESP_OK) {
        ESP_LOGD(TAG, "state %s => %s", subtopic, payload);
    } else if (rc == ESP_ERR_INVALID_STATE) {
        /* Broker not yet connected (or MQTT never started). S3 logs
         * "MQTT not connected" at ERROR; we keep it at DEBUG because
         * pre-connect touches (during app_ui_init) would otherwise spam. */
        ESP_LOGD(TAG, "mqtt not up, dropped %s => %s", subtopic, payload);
    } else {
        ESP_LOGW(TAG, "mqtt publish failed (%d) %s => %s", rc, subtopic, payload);
    }
}

/* ==================== step 7B: statusupdate ==================== */
/* S3 hasp_dispatch.cpp:1473 dispatch_statusupdate.
 *
 * Payload adapted to what the p4-abrom stack can actually query today:
 *   S3 field           | p4-abrom source                | notes
 *   -------------------+---------------------------------+---------------------
 *   node               | wifi hostname (netif)          | S3 haspDevice.get_hostname
 *   version            | IDF_VER + git ver placeholder  | no HASP_VERSION define yet
 *   uptime             | esp_timer_get_time()/1e6       | S3 millis()/1000
 *   ip / rssi / ssid   | esp_netif + esp_wifi_sta_get_ap| S3 network_get_statusupdate
 *   heapFree           | esp_get_free_heap_size()       | S3 haspDevice.get_free_heap
 *   heapMin            | esp_get_minimum_free_heap_size | replaces S3 heapFrag
 *                      |                                 | (no equivalent API on IDF)
 *   page / numPages    | haspPages.get() / HASP_NUM_PAGES| S3 identical
 *   tftWidth / Height  | lv_display_get_default()       | S3 haspTft.width/height
 *
 * S3-only fields intentionally omitted (features not yet ported to p4-abrom):
 *   - idle    (sleep_state — sleep/antiburn not implemented)
 *   - core    (Arduino ESP core string — n/a for ESP-IDF)
 *   - canUpdate (OTA — deferred)
 *   - tftDriver (JD9165 hardcoded via BSP — no runtime getter)
 *
 * Topic: hasp/<host>/state/statusupdate  (via dispatch_state_subtopic).
 */
void hasp_dispatch_statusupdate(void)
{
    /* Gather network + system state on stack. All calls are cheap and safe
     * off-lock, but we're already under the LVGL lock (called from the 1s
     * timer or a command dispatch). */
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    const char* hostname = "";
    if (sta) esp_netif_get_hostname(sta, &hostname);
    if (!hostname) hostname = "";

    char ip_str[16] = "";
    if (sta) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(sta, &info) == ESP_OK) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&info.ip));
        }
    }

    /* esp_wifi_sta_get_ap_info returns ESP_ERR_WIFI_NOT_CONNECT until associated.
     * On P4 the call goes through esp_wifi_remote → C6; still returns instantly
     * (no radio blocking). */
    wifi_ap_record_t ap = {};
    int  rssi = 0;
    const char* ssid = "";
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
        ssid = (const char*)ap.ssid;
    }

    uint64_t uptime_s   = esp_timer_get_time() / 1000000ULL;
    uint32_t heap_free  = (uint32_t)esp_get_free_heap_size();
    uint32_t heap_min   = (uint32_t)esp_get_minimum_free_heap_size();
    uint8_t  page_now   = hasp_get_page();

    int32_t tft_w = 0, tft_h = 0;
    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        tft_w = lv_display_get_horizontal_resolution(disp);
        tft_h = lv_display_get_vertical_resolution(disp);
    }

    /* S3 uses a 400-byte char buffer + snprintf_P chunks. We stream through
     * ArduinoJson for readability — payload is well under 400 bytes even with
     * long SSID/hostname (checked below). */
    JsonDocument doc;
    doc["node"]      = hostname;
    doc["version"]   = "p4-abrom " IDF_VER;
    doc["uptime"]    = (uint32_t)uptime_s;
    doc["ip"]        = ip_str;
    doc["ssid"]      = ssid;
    doc["rssi"]      = rssi;
    doc["heapFree"]  = heap_free;
    doc["heapMin"]   = heap_min;
    doc["page"]      = page_now;
    doc["numPages"]  = (uint16_t)HASP_NUM_PAGES;
    doc["tftWidth"]  = tft_w;
    doc["tftHeight"] = tft_h;

    char buf[400];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        ESP_LOGW(TAG, "statusupdate: JSON overflow (n=%u)", (unsigned)n);
        return;
    }

    dispatch_state_subtopic("statusupdate", buf);
}

/* ==================== step 7D-1: sensordata ==================== */
/* S3 hasp_dispatch.cpp:1341 dispatch_send_sensordata.
 *
 * S3 payload:
 *   time      — strftime("%FT%T", localtime(&raw))     (unconditional; before
 *                                                       SNTP sync it emits a
 *                                                       1970 timestamp — same
 *                                                       here, kept for shape
 *                                                       parity)
 *   uptimeSec — haspDevice.get_uptime()                (esp_timer on P4)
 *   uptime    — "%dT%02d:%02d:%02d" formatted          (identical formatter)
 *   <sensors> — haspDevice.get_sensors(doc)            (LM75/SHT31 etc — no
 *                                                       hardware sensor bus
 *                                                       ported to p4-abrom
 *                                                       yet, so this stays
 *                                                       empty; adding it
 *                                                       later is a matter of
 *                                                       plugging get_sensors()
 *                                                       here and NOT changing
 *                                                       the topic)
 *   <custom>  — custom_get_sensors(doc) if HASP_USE_CUSTOM (not ported)
 *
 * Topic: hasp/<host>/state/sensors  (S3 mqtt_send_state(MQTT_TOPIC_SENSORS)
 * → same state/ prefix as statusupdate, so we reuse dispatch_state_subtopic
 * without adding a new mqtt path — that's what makes 7D-1 the "easy half"
 * of 7D; discovery in 7D-2 needs the separate hasp/discovery/<hwid> path).
 *
 * Reset cadence: `= teleperiod` (S3 line 1399 — identical to statusupdate,
 * not the *2+random used for discovery).
 */
void hasp_dispatch_send_sensordata(void)
{
    JsonDocument doc;

    /* Time — S3 unconditional strftime. Before SNTP sync (p4-abrom does not
     * start SNTP yet) time() returns 0, so `time` will read "1970-01-01T00…"
     * — cosmetic only; drop-in SNTP later fixes it without touching this
     * code or the topic contract. */
    time_t rawtime = 0;
    time(&rawtime);
    char timebuf[32];
    struct tm timeinfo;
    localtime_r(&rawtime, &timeinfo);
    strftime(timebuf, sizeof(timebuf), "%FT%T", &timeinfo);
    doc["time"] = timebuf;

    uint64_t uptime_s = esp_timer_get_time() / 1000000ULL;
    doc["uptimeSec"]  = (uint32_t)uptime_s;

    /* Same formatter as S3 (line 1364) so downstream consumers that parse
     * "<days>T<hh>:<mm>:<ss>" keep working. */
    uint32_t secs  = (uint32_t)(uptime_s % 60);
    uint32_t mins  = (uint32_t)(uptime_s / 60);
    uint32_t hours = mins / 60;
    uint32_t days  = hours / 24;
    mins  = mins  % 60;
    hours = hours % 24;
    char upbuf[24];
    snprintf(upbuf, sizeof(upbuf), "%uT%02u:%02u:%02u",
             (unsigned)days, (unsigned)hours, (unsigned)mins, (unsigned)secs);
    doc["uptime"] = upbuf;

    /* No haspDevice.get_sensors() equivalent on p4-abrom yet — the plate has
     * no ADCs/i2c sensors wired via the HASP HAL. Field intentionally omitted
     * rather than emitting an empty object, matching how S3 behaves on boards
     * with no HASP_USE_SENSORS at build time. */

    char buf[256];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        ESP_LOGW(TAG, "sensors: JSON overflow (n=%u)", (unsigned)n);
        return;
    }

    dispatch_state_subtopic("sensors", buf);
}

/* S3 hasp_dispatch.cpp:1761 dispatchEverySecond. Two teleperiod-driven
 * publishes so far (statusupdate + sensors); discovery lands in 7D-2 with
 * its own *2+random cadence. Both branches share the same MQTT-connected
 * gate — while offline we hold each counter at 1 so the first tick after
 * reconnect fires immediately (mirrors S3 mqttIsConnected() short-circuit
 * inside the else branch). */
void hasp_every_second(void)
{
    if (s_teleperiod_s == 0) return;                       // disabled

    /* --- statusupdate (step 7B) --- */
    if (s_seconds_to_next_teleperiod > 1) {
        s_seconds_to_next_teleperiod--;
    } else if (!hasp_mqtt_is_connected()) {
        s_seconds_to_next_teleperiod = 1;
    } else {
        hasp_dispatch_statusupdate();
        s_seconds_to_next_teleperiod = s_teleperiod_s;
    }

    /* --- sensordata (step 7D-1) --- */
    if (s_seconds_to_next_sensordata > 1) {
        s_seconds_to_next_sensordata--;
    } else if (!hasp_mqtt_is_connected()) {
        s_seconds_to_next_sensordata = 1;
    } else {
        hasp_dispatch_send_sensordata();
        s_seconds_to_next_sensordata = s_teleperiod_s;
    }
}

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
    /* Step 7B: on-demand telemetry (S3 line 1705
     * dispatch_add_command("statusupdate", dispatch_statusupdate)). Any
     * payload is ignored — the command exists purely to force an immediate
     * publish. Reset the periodic counter so we don't publish twice in a
     * row on the next tick. */
    if (!strcasecmp(topic, "statusupdate")) {
        hasp_dispatch_statusupdate();
        s_seconds_to_next_teleperiod = s_teleperiod_s;
        return;
    }
    /* Step 7D-1: S3 line 1708 dispatch_add_command("sensors", …). Forces an
     * immediate publish and rearms the periodic counter so we don't emit
     * twice on the next tick. */
    if (!strcasecmp(topic, "sensors")) {
        hasp_dispatch_send_sensordata();
        s_seconds_to_next_sensordata = s_teleperiod_s;
        return;
    }

    ESP_LOGW(TAG, "command not found: %s => %s", topic, payload);
}
