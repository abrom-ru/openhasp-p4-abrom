/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (3a-refactor): Parser::get_sdbm() only for now. */

#ifndef HASP_PARSER_H
#define HASP_PARSER_H

#include <stdint.h>

class Parser {
  public:
    static uint16_t get_sdbm(const char* str);
};

#endif
