#include "lua_utils.h"
#include "lauxlib.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ── String Helpers ──────────────────────────────────────────────────────── */

static int l_string_trim(lua_State *L) {
    size_t len = 0;
    const char *s = luaL_checklstring(L, 1, &len);
    if (len == 0) {
        lua_pushliteral(L, "");
        return 1;
    }

    size_t start = 0;
    while (start < len && isspace((unsigned char)s[start])) {
        start++;
    }

    if (start == len) {
        lua_pushliteral(L, "");
        return 1;
    }

    size_t end = len - 1;
    while (end > start && isspace((unsigned char)s[end])) {
        end--;
    }

    lua_pushlstring(L, s + start, end - start + 1);
    return 1;
}

static int l_string_clean(lua_State *L) {
    size_t len = 0;
    const char *s = luaL_checklstring(L, 1, &len);
    if (len == 0) {
        lua_pushliteral(L, "");
        return 1;
    }

    char stack_buf[512];
    char *out = (len < sizeof(stack_buf)) ? stack_buf : (char *)malloc(len + 1);
    if (!out) {
        return luaL_error(L, "out of memory in string_clean");
    }

    size_t out_len = 0;
    bool in_space = false;
    size_t i = 0;

    /* Skip leading whitespace */
    while (i < len && isspace((unsigned char)s[i])) {
        i++;
    }

    for (; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isspace(c)) {
            if (!in_space) {
                out[out_len++] = ' ';
                in_space = true;
            }
        } else {
            out[out_len++] = (char)c;
            in_space = false;
        }
    }

    /* Remove trailing space if any */
    if (out_len > 0 && out[out_len - 1] == ' ') {
        out_len--;
    }

    lua_pushlstring(L, out, out_len);
    if (out != stack_buf) {
        free(out);
    }
    return 1;
}

static int l_string_starts_with(lua_State *L) {
    size_t slen = 0, plen = 0;
    const char *s = luaL_checklstring(L, 1, &slen);
    const char *prefix = luaL_checklstring(L, 2, &plen);

    if (plen > slen) {
        lua_pushboolean(L, false);
    } else {
        lua_pushboolean(L, memcmp(s, prefix, plen) == 0);
    }
    return 1;
}

static int l_string_ends_with(lua_State *L) {
    size_t slen = 0, suflen = 0;
    const char *s = luaL_checklstring(L, 1, &slen);
    const char *suffix = luaL_checklstring(L, 2, &suflen);

    if (suflen > slen) {
        lua_pushboolean(L, false);
    } else {
        lua_pushboolean(L, memcmp(s + slen - suflen, suffix, suflen) == 0);
    }
    return 1;
}

static int l_string_split(lua_State *L) {
    size_t slen = 0, seplen = 0;
    const char *s = luaL_checklstring(L, 1, &slen);
    const char *sep = luaL_checklstring(L, 2, &seplen);

    lua_newtable(L);
    int count = 0;

    if (seplen == 0) {
        /* Split by each character */
        for (size_t i = 0; i < slen; i++) {
            lua_pushlstring(L, s + i, 1);
            lua_rawseti(L, -2, ++count);
        }
        return 1;
    }

    const char *curr = s;
    const char *end = s + slen;
    const char *match;

    while (curr <= end) {
        match = (const char *)strstr(curr, sep);
        if (!match || match > end) {
            lua_pushlstring(L, curr, (size_t)(end - curr));
            lua_rawseti(L, -2, ++count);
            break;
        }
        lua_pushlstring(L, curr, (size_t)(match - curr));
        lua_rawseti(L, -2, ++count);
        curr = match + seplen;
    }

    return 1;
}

static int l_string_normalize(lua_State *L) {
    /* Pass through or clean */
    return l_string_clean(L);
}

/* ── Unicode Unescaping ──────────────────────────────────────────────────── */

static int encode_utf8(unsigned int cp, char *out) {
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static int l_unescape_unicode(lua_State *L) {
    size_t len = 0;
    const char *s = luaL_checklstring(L, 1, &len);

    luaL_Buffer b;
    luaL_buffinit(L, &b);

    size_t i = 0;
    while (i < len) {
        if (s[i] == '\\' && i + 1 < len && s[i + 1] == 'u' && i + 5 < len) {
            unsigned int cp = 0;
            if (sscanf(s + i + 2, "%4x", &cp) == 1) {
                i += 6;
                /* Check surrogate pair */
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 5 < len && s[i] == '\\' && s[i + 1] == 'u') {
                    unsigned int low = 0;
                    if (sscanf(s + i + 2, "%4x", &low) == 1 && low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i += 6;
                    }
                }
                char utf8[4];
                int ulen = encode_utf8(cp, utf8);
                if (ulen > 0) {
                    luaL_addlstring(&b, utf8, (size_t)ulen);
                    continue;
                }
            }
        }
        luaL_addchar(&b, s[i++]);
    }

    luaL_pushresult(&b);
    return 1;
}

/* ── URL Helpers ─────────────────────────────────────────────────────────── */

static int l_url_encode(lua_State *L) {
    size_t len = 0;
    const char *s = luaL_checklstring(L, 1, &len);

    luaL_Buffer b;
    luaL_buffinit(L, &b);

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            luaL_addchar(&b, (char)c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            luaL_addlstring(&b, buf, 3);
        }
    }

    luaL_pushresult(&b);
    return 1;
}

static int l_url_resolve(lua_State *L) {
    size_t blen = 0, rlen = 0;
    const char *base = luaL_checklstring(L, 1, &blen);
    const char *rel = luaL_checklstring(L, 2, &rlen);

    if (rlen == 0) {
        lua_pushlstring(L, base, blen);
        return 1;
    }

    /* 1. Already an absolute URL */
    if (strncmp(rel, "http://", 7) == 0 ||
        strncmp(rel, "https://", 8) == 0 ||
        strncmp(rel, "ftp://", 6) == 0) {
        lua_pushlstring(L, rel, rlen);
        return 1;
    }

    /* 2. Protocol-relative //example.com/path */
    if (strncmp(rel, "//", 2) == 0) {
        const char *scheme_end = strstr(base, "://");
        if (scheme_end) {
            size_t sch_len = (size_t)(scheme_end - base) + 1; /* includes ':' */
            luaL_Buffer b;
            luaL_buffinit(L, &b);
            luaL_addlstring(&b, base, sch_len);
            luaL_addlstring(&b, rel, rlen);
            luaL_pushresult(&b);
            return 1;
        } else {
            luaL_Buffer b;
            luaL_buffinit(L, &b);
            luaL_addstring(&b, "https:");
            luaL_addlstring(&b, rel, rlen);
            luaL_pushresult(&b);
            return 1;
        }
    }

    /* Find scheme and host end in base */
    const char *scheme_end = strstr(base, "://");
    if (!scheme_end) {
        /* Base is not a full URL, fallback: base + "/" + rel */
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        luaL_addlstring(&b, base, blen);
        if (blen > 0 && base[blen - 1] != '/') luaL_addchar(&b, '/');
        luaL_addlstring(&b, rel, rlen);
        luaL_pushresult(&b);
        return 1;
    }

    const char *host_start = scheme_end + 3;
    const char *first_slash = strchr(host_start, '/');
    size_t origin_len = first_slash ? (size_t)(first_slash - base) : blen;

    /* 3. Root-relative /path */
    if (rel[0] == '/') {
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        luaL_addlstring(&b, base, origin_len);
        luaL_addlstring(&b, rel, rlen);
        luaL_pushresult(&b);
        return 1;
    }

    /* 4. Directory-relative path */
    const char *last_slash = strrchr(base, '/');
    if (!last_slash || last_slash < scheme_end + 2) {
        /* base has no path after origin, e.g. https://site.com */
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        luaL_addlstring(&b, base, blen);
        luaL_addchar(&b, '/');
        luaL_addlstring(&b, rel, rlen);
        luaL_pushresult(&b);
        return 1;
    }

    size_t dir_len = (size_t)(last_slash - base) + 1;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    luaL_addlstring(&b, base, dir_len);
    luaL_addlstring(&b, rel, rlen);
    luaL_pushresult(&b);
    return 1;
}

/* ── Callable String Metatable (__call) ──────────────────────────────────── */

static int l_str_call(lua_State *L) {
    /* When string is called like s(...) -> returns s */
    lua_pushvalue(L, 1);
    return 1;
}

static void setup_string_call(lua_State *L) {
    lua_pushliteral(L, "");
    if (lua_getmetatable(L, -1)) {
        lua_pushcfunction(L, l_str_call);
        lua_setfield(L, -2, "__call");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

/* ── Module Registration ─────────────────────────────────────────────────── */

int luaopen_utils(lua_State *L) {
    /* Register globals */
    lua_pushcfunction(L, l_string_clean);
    lua_setglobal(L, "string_clean");

    lua_pushcfunction(L, l_string_trim);
    lua_setglobal(L, "string_trim");

    lua_pushcfunction(L, l_string_starts_with);
    lua_setglobal(L, "string_starts_with");

    lua_pushcfunction(L, l_string_ends_with);
    lua_setglobal(L, "string_ends_with");

    lua_pushcfunction(L, l_string_split);
    lua_setglobal(L, "string_split");

    lua_pushcfunction(L, l_string_normalize);
    lua_setglobal(L, "string_normalize");

    lua_pushcfunction(L, l_unescape_unicode);
    lua_setglobal(L, "unescape_unicode");

    lua_pushcfunction(L, l_url_encode);
    lua_setglobal(L, "url_encode");

    lua_pushcfunction(L, l_url_encode);
    lua_setglobal(L, "url_encode_charset");

    lua_pushcfunction(L, l_url_resolve);
    lua_setglobal(L, "url_resolve");

    /* Setup string callable metatable */
    setup_string_call(L);

    return 0;
}
