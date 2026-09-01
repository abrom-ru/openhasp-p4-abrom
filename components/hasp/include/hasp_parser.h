/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (3a→3f):
 *   3a: Parser::get_sdbm() (verbatim from S3 src/hasp/hasp_parser.cpp:172).
 *   3e: Parser::is_true / Parser::haspPayloadToColor — the two Parser methods
 *       actually used by hasp_attribute (S3 src/hasp/hasp_parser.cpp:18 / :180).
 *       Named-color table is deferred (needs haspNamedColors[]); hex #rrggbb /
 *       #rgb and pure-digit RGB565 are supported — matches S3 branches, sans
 *       the named-colors loop.
 *   3f: Parser::get_event_name / get_event_state — used by hasp_event to build
 *       the state topic JSON payload (S3 src/hasp/hasp_parser.cpp:116 / :135).
 */

#ifndef HASP_PARSER_H
#define HASP_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"

class Parser {
  public:
    static uint16_t get_sdbm(const char* str);
    static bool     is_true(const char* s);
    /* Fills LVGL 9 lv_color32_t (fields: .red/.green/.blue/.alpha, no
     * `.ch.*` union — that was LVGL 7). Returns false on unrecognized input. */
    static bool     haspPayloadToColor(const char* payload, lv_color32_t& color);

    /* Event helpers (3f) — 1-в-1 с S3 src/hasp/hasp_parser.cpp:116/:135.
     * `buffer` must be ≥8 chars ("release"/"changed" + \0). */
    static void     get_event_name(uint8_t eventid, char* buffer, size_t size);
    static bool     get_event_state(uint8_t eventid);
};

#endif
