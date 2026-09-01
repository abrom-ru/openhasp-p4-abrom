/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (3a→3f): mirrors src/hasp/hasp_parser.cpp
 *  - get_sdbm           (S3 line 172) — verbatim.
 *  - is_true            (S3 line 180) — verbatim minus PSTR wrappers.
 *  - haspPayloadToColor (S3 line 18)  — hex + RGB565 only, named colors dropped
 *    (haspNamedColors[] table not yet ported — comes with theme/color widgets).
 *    LVGL 9 lv_color32_t field access is `.red/.green/.blue` (not `.ch.red`).
 *  - get_event_name     (S3 line 135) — verbatim, PSTR/memcpy_P → plain strcpy.
 *  - get_event_state    (S3 line 116) — verbatim.
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include "hasp_parser.h"
#include "hasp_event.h" /* HASP_EVENT_* enum values */

/* 16-bit hashing function http://www.cse.yorku.ca/~oz/hash.html */
uint16_t Parser::get_sdbm(const char* str)
{
    uint16_t hash = 0;
    while (char c = tolower(*str++))
        if (c > 57 || c < 48) hash = c + (hash << 6) - hash; // exclude digits (collision risk)
    return hash;
}

bool Parser::is_true(const char* s)
{
    if (!s) return false;
    return (!strcasecmp(s, "true") || !strcasecmp(s, "on") ||
            !strcasecmp(s, "yes")  || !strcmp(s, "1"));
}

static inline bool is_only_digits(const char* s)
{
    if (!s || !*s) return false;
    while (*s) { if (!isdigit((unsigned char)*s)) return false; s++; }
    return true;
}

bool Parser::haspPayloadToColor(const char* payload, lv_color32_t& color)
{
    if (!payload) return false;

    /* HEX format #rrggbb or #rgb */
    if (*payload == '#') {
        size_t len = strlen(payload);
        if (len >= 8) return false;

        char* pEnd;
        long color_int = strtol(payload + 1, &pEnd, 16);

        if (pEnd - payload == 7) {          /* #rrggbb */
            color.red   = (color_int >> 16) & 0xff;
            color.green = (color_int >> 8)  & 0xff;
            color.blue  = (color_int)       & 0xff;
        } else if (pEnd - payload == 4) {   /* #rgb */
            color.red   = (color_int >> 8) & 0xf;
            color.green = (color_int >> 4) & 0xf;
            color.blue  = (color_int)      & 0xf;
            color.red   += color.red   * 0x10;
            color.green += color.green * 0x10;
            color.blue  += color.blue  * 0x10;
        } else {
            return false;
        }
        color.alpha = 0xff;
        return true;
    }

    /* 16-bit RGB565 Color Scheme */
    if (is_only_digits(payload)) {
        uint16_t c = (uint16_t)atoi(payload);
        uint8_t R5 = ((c >> 11) & 0b11111);
        uint8_t G6 = ((c >> 5)  & 0b111111);
        uint8_t B5 = ((c)       & 0b11111);
        color.red   = (R5 * 527 + 23) >> 6;
        color.green = (G6 * 259 + 33) >> 6;
        color.blue  = (B5 * 527 + 23) >> 6;
        color.alpha = 0xff;
        return true;
    }

    /* Named colors — deferred (haspNamedColors[] not ported yet). */
    return false;
}

/* Map an eventid to its ON/OFF state — mirrors S3 hasp_parser.cpp:116.
 * Used by 3f event handlers before publishing group updates. */
bool Parser::get_event_state(uint8_t eventid)
{
    switch (eventid) {
        case HASP_EVENT_ON:
        case HASP_EVENT_DOWN:
        case HASP_EVENT_LONG:
        case HASP_EVENT_HOLD:
            return true;
        default:
            return false;
    }
}

/* Map an eventid to its description string — mirrors S3 hasp_parser.cpp:135.
 * `buffer` must be ≥8 chars ("release"/"changed" + \0). */
void Parser::get_event_name(uint8_t eventid, char* buffer, size_t size)
{
    if (!buffer || size == 0) return;
    const char* name;
    switch (eventid) {
        case HASP_EVENT_ON:      name = "on";      break;
        case HASP_EVENT_OFF:     name = "off";     break;
        case HASP_EVENT_UP:      name = "up";      break;
        case HASP_EVENT_DOWN:    name = "down";    break;
        case HASP_EVENT_RELEASE: name = "release"; break;
        case HASP_EVENT_LONG:    name = "long";    break;
        case HASP_EVENT_HOLD:    name = "hold";    break;
        case HASP_EVENT_LOST:    name = "lost";    break;
        case HASP_EVENT_CHANGED: name = "changed"; break;
        default:                 name = "unknown"; break;
    }
    strncpy(buffer, name, size - 1);
    buffer[size - 1] = '\0';
}
