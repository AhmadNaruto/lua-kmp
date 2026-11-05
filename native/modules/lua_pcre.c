#include "lua_pcre.h"
#include "lauxlib.h"
#include <stdlib.h>
#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"

#define REGEX_CACHE_SIZE 32

typedef struct {
    char *pattern;
    size_t pat_len;
    pcre2_code *code;
} regex_cache_entry_t;

static regex_cache_entry_t s_regex_cache[REGEX_CACHE_SIZE];
static int s_cache_head = 0;

static pcre2_code *get_cached_regex(const char *pat, size_t pat_len) {
    for (int i = 0; i < REGEX_CACHE_SIZE; i++) {
        if (s_regex_cache[i].code &&
            s_regex_cache[i].pat_len == pat_len &&
            memcmp(s_regex_cache[i].pattern, pat, pat_len) == 0) {
            return s_regex_cache[i].code;
        }
    }

    int errornumber;
    PCRE2_SIZE erroroffset;
    pcre2_code *re = pcre2_compile(
        (PCRE2_SPTR)pat,
        pat_len,
        PCRE2_UTF,
        &errornumber,
        &erroroffset,
        NULL
    );

    if (!re) {
        return NULL;
    }

    int slot = s_cache_head;
    s_cache_head = (s_cache_head + 1) % REGEX_CACHE_SIZE;

    if (s_regex_cache[slot].code) {
        pcre2_code_free(s_regex_cache[slot].code);
        free(s_regex_cache[slot].pattern);
        s_regex_cache[slot].code = NULL;
        s_regex_cache[slot].pattern = NULL;
    }

    s_regex_cache[slot].pattern = (char *)malloc(pat_len + 1);
    if (s_regex_cache[slot].pattern) {
        memcpy(s_regex_cache[slot].pattern, pat, pat_len);
        s_regex_cache[slot].pattern[pat_len] = '\0';
        s_regex_cache[slot].pat_len = pat_len;
        s_regex_cache[slot].code = re;
    }

    return re;
}

/*
** regex_match(text, pattern) -> table of matched substrings
** Compatible with NoveLA's Regex(pattern).findAll(text).
*/
static int l_regex_match(lua_State *L) {
    size_t text_len = 0, pat_len = 0;
    const char *text = luaL_checklstring(L, 1, &text_len);
    const char *pat = luaL_checklstring(L, 2, &pat_len);

    lua_newtable(L);
    if (!text || !pat || pat_len == 0) {
        return 1;
    }

    pcre2_code *re = get_cached_regex(pat, pat_len);
    if (!re) {
        /* On invalid regex, return empty table */
        return 1;
    }

    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, NULL);
    if (!match_data) {
        return 1;
    }

    PCRE2_SIZE offset = 0;
    int count = 0;

    while (offset <= text_len) {
        int rc = pcre2_match(re, (PCRE2_SPTR)text, text_len, offset, 0, match_data, NULL);
        if (rc < 0) {
            break;
        }

        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
        PCRE2_SIZE start = ovector[0];
        PCRE2_SIZE end = ovector[1];

        if (start != PCRE2_UNSET && end != PCRE2_UNSET && end >= start) {
            count++;
            lua_pushlstring(L, text + start, end - start);
            lua_rawseti(L, -2, count);
        }

        if (end > offset) {
            offset = end;
        } else {
            offset++;
        }
    }

    pcre2_match_data_free(match_data);
    return 1;
}

/*
** regex_replace(text, pattern, replacement) -> replaced string
** Compatible with NoveLA's text.replace(Regex(pattern), replacement).
*/
static int l_regex_replace(lua_State *L) {
    size_t text_len = 0, pat_len = 0, repl_len = 0;
    const char *text = luaL_checklstring(L, 1, &text_len);
    const char *pat = luaL_checklstring(L, 2, &pat_len);
    const char *repl = luaL_checklstring(L, 3, &repl_len);

    if (!text || !pat || pat_len == 0) {
        lua_pushvalue(L, 1);
        return 1;
    }

    pcre2_code *re = get_cached_regex(pat, pat_len);
    if (!re) {
        lua_pushvalue(L, 1);
        return 1;
    }

    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, NULL);
    if (!match_data) {
        lua_pushvalue(L, 1);
        return 1;
    }

    /* Stack buffer for common replacements, heap for large */
    char stack_buf[512];
    size_t out_cap = text_len * 2 + repl_len + 64;
    char *out_buf = (out_cap <= sizeof(stack_buf)) ? stack_buf : (char *)malloc(out_cap);
    if (!out_buf) {
        pcre2_match_data_free(match_data);
        lua_pushvalue(L, 1);
        return 1;
    }

    PCRE2_SIZE out_len = out_cap;
    int rc = pcre2_substitute(
        re,
        (PCRE2_SPTR)text,
        text_len,
        0,
        PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED,
        match_data,
        NULL,
        (PCRE2_SPTR)repl,
        repl_len,
        (PCRE2_UCHAR *)out_buf,
        &out_len
    );

    if (rc == PCRE2_ERROR_NOMEMORY) {
        /* Buffer was too small, out_len has required size */
        if (out_buf != stack_buf) free(out_buf);
        out_cap = out_len;
        out_buf = (char *)malloc(out_cap);
        if (out_buf) {
            rc = pcre2_substitute(
                re,
                (PCRE2_SPTR)text,
                text_len,
                0,
                PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED,
                match_data,
                NULL,
                (PCRE2_SPTR)repl,
                repl_len,
                (PCRE2_UCHAR *)out_buf,
                &out_len
            );
        }
    }

    if (rc >= 0 && out_buf) {
        lua_pushlstring(L, out_buf, out_len);
    } else {
        lua_pushvalue(L, 1);
    }

    if (out_buf && out_buf != stack_buf) free(out_buf);
    pcre2_match_data_free(match_data);
    return 1;
}

int luaopen_pcre(lua_State *L) {
    lua_pushcfunction(L, l_regex_match);
    lua_setglobal(L, "regex_match");

    lua_pushcfunction(L, l_regex_replace);
    lua_setglobal(L, "regex_replace");

    return 0;
}
