#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "charset/aliases.h"

uint16_t parserutils_charset_mibenum_from_name(const char *alias, size_t len) {
    return 106; // 強制回傳 UTF-8
}

const char *parserutils_charset_mibenum_to_name(uint16_t mibenum) {
    return "UTF-8";
}

bool parserutils_charset_mibenum_is_unicode(uint16_t mibenum) {
    return true;
}

parserutils_charset_aliases_canon *parserutils__charset_alias_canonicalise(const char *alias, size_t len) {
    static parserutils_charset_aliases_canon utf8 = { 106, 5, "UTF-8" };
    return &utf8;
}
