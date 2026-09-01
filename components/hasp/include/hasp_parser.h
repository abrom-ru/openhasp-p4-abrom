/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (3a→3e):
 *   3a: Parser::get_sdbm() (verbatim from S3 src/hasp/hasp_parser.cpp:172).
 *   3e: Parser::is_true / Parser::haspPayloadToColor — the two Parser methods
 *       actually used by hasp_attribute (S3 src/hasp/hasp_parser.cpp:18 / :180).
 *       Named-color table is deferred (needs haspNamedColors[]); hex #rrggbb /
 *       #rgb and pure-digit RGB565 are supported — matches S3 branches, sans
 *       the named-colors loop.
 */

#ifndef HASP_PARSER_H
#define HASP_PARSER_H

#include <stdint.h>
#include "lvgl.h"

class Parser {
  public:
    static uint16_t get_sdbm(const char* str);
    static bool     is_true(const char* s);
    /* Fills LVGL 9 lv_color32_t (fields: .red/.green/.blue/.alpha, no
     * `.ch.*` union — that was LVGL 7). Returns false on unrecognized input. */
    static bool     haspPayloadToColor(const char* payload, lv_color32_t& color);
};

#endif
