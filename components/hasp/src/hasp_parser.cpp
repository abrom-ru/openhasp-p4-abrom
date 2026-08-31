/* MIT License - Copyright (c) 2019-2024 Francis Van Roie
   For full license information read the LICENSE file in the project folder */
/* Ported to p4-abrom (3a-refactor) 1-to-1 from src/hasp/hasp_parser.cpp:172 */

#include <ctype.h>

#include "hasp_parser.h"

/* 16-bit hashing function http://www.cse.yorku.ca/~oz/hash.html */
/* all possible attributes are hashed and checked if they are unique */
uint16_t Parser::get_sdbm(const char* str)
{
    uint16_t hash = 0;
    while (char c = tolower(*str++))
        if (c > 57 || c < 48) hash = c + (hash << 6) - hash; // exclude numbers which can cause collisions
    return hash;
}
