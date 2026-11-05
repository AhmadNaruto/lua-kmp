#include "lua_json.h"
#include "yyjson.h"
#include "lauxlib.h"
#include <string.h>

static void push_yyjson_val(lua_State *L, yyjson_val *val) {
    if (!val) {
        lua_pushnil(L);
        return;
    }

    switch (yyjson_get_type(val)) {
        case YYJSON_TYPE_NULL:
            lua_pushnil(L);
            break;
        case YYJSON_TYPE_BOOL:
            lua_pushboolean(L, yyjson_get_bool(val));
            break;
        case YYJSON_TYPE_NUM:
            if (yyjson_is_int(val)) {
                lua_pushinteger(L, (lua_Integer)yyjson_get_int(val));
            } else if (yyjson_is_uint(val)) {
                lua_pushinteger(L, (lua_Integer)yyjson_get_uint(val));
            } else {
                lua_pushnumber(L, (lua_Number)yyjson_get_num(val));
            }
            break;
        case YYJSON_TYPE_STR:
            lua_pushlstring(L, yyjson_get_str(val), yyjson_get_len(val));
            break;
        case YYJSON_TYPE_ARR: {
            size_t idx, max = yyjson_arr_size(val);
            yyjson_val *item;
            lua_createtable(L, (int)max, 0);
            yyjson_arr_foreach(val, idx, max, item) {
                push_yyjson_val(L, item);
                lua_rawseti(L, -2, (lua_Integer)(idx + 1));
            }
            break;
        }
        case YYJSON_TYPE_OBJ: {
            size_t idx, max = yyjson_obj_size(val);
            yyjson_val *key, *item;
            lua_createtable(L, 0, (int)max);
            yyjson_obj_foreach(val, idx, max, key, item) {
                lua_pushlstring(L, yyjson_get_str(key), yyjson_get_len(key));
                push_yyjson_val(L, item);
                lua_rawset(L, -3);
            }
            break;
        }
        default:
            lua_pushnil(L);
            break;
    }
}

static int l_json_parse(lua_State *L) {
    size_t len = 0;
    const char *str = luaL_checklstring(L, 1, &len);
    if (!str || len == 0) {
        lua_pushnil(L);
        return 1;
    }

    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)str, len, YYJSON_READ_NOFLAG, NULL, &err);
    if (!doc) {
        /* Parse error: return nil, error_message */
        lua_pushnil(L);
        lua_pushstring(L, err.msg ? err.msg : "JSON syntax error");
        return 2;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    push_yyjson_val(L, root);
    yyjson_doc_free(doc);
    return 1;
}

static yyjson_mut_val *lua_to_yyjson_mut(lua_State *L, int idx, yyjson_mut_doc *doc, int depth) {
    if (depth > 64) {
        return yyjson_mut_null(doc);
    }

    idx = lua_absindex(L, idx);
    int type = lua_type(L, idx);

    switch (type) {
        case LUA_TNIL:
            return yyjson_mut_null(doc);
        case LUA_TBOOLEAN:
            return yyjson_mut_bool(doc, lua_toboolean(L, idx));
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) {
                return yyjson_mut_sint(doc, (int64_t)lua_tointeger(L, idx));
            } else {
                return yyjson_mut_real(doc, (double)lua_tonumber(L, idx));
            }
        case LUA_TSTRING: {
            size_t len = 0;
            const char *s = lua_tolstring(L, idx, &len);
            return yyjson_mut_strn(doc, s, len);
        }
        case LUA_TTABLE: {
            /* Check if table is array or object */
            lua_len(L, idx);
            lua_Integer len = lua_tointeger(L, -1);
            lua_pop(L, 1);

            if (len > 0) {
                /* Array */
                yyjson_mut_val *arr = yyjson_mut_arr(doc);
                for (lua_Integer i = 1; i <= len; i++) {
                    lua_rawgeti(L, idx, i);
                    yyjson_mut_val *item = lua_to_yyjson_mut(L, -1, doc, depth + 1);
                    yyjson_mut_arr_append(arr, item);
                    lua_pop(L, 1);
                }
                return arr;
            } else {
                /* Object or empty table */
                yyjson_mut_val *obj = yyjson_mut_obj(doc);
                lua_pushnil(L);
                while (lua_next(L, idx) != 0) {
                    /* Key is at -2, value at -1 */
                    if (lua_type(L, -2) == LUA_TSTRING) {
                        size_t klen = 0;
                        const char *k = lua_tolstring(L, -2, &klen);
                        yyjson_mut_val *val = lua_to_yyjson_mut(L, -1, doc, depth + 1);
                        yyjson_mut_obj_add(obj, yyjson_mut_strn(doc, k, klen), val);
                    } else if (lua_isinteger(L, -2)) {
                        char num_key[32];
                        snprintf(num_key, sizeof(num_key), "%lld", (long long)lua_tointeger(L, -2));
                        yyjson_mut_val *val = lua_to_yyjson_mut(L, -1, doc, depth + 1);
                        yyjson_mut_obj_add(obj, yyjson_mut_str(doc, num_key), val);
                    }
                    lua_pop(L, 1);
                }
                return obj;
            }
        }
        default:
            return yyjson_mut_null(doc);
    }
}

static int l_json_stringify(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        lua_pushliteral(L, "null");
        return 1;
    }

    int pretty = lua_toboolean(L, 2);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        lua_pushnil(L);
        return 1;
    }

    yyjson_mut_val *root = lua_to_yyjson_mut(L, 1, doc, 0);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_write_flag flg = pretty ? YYJSON_WRITE_PRETTY : YYJSON_WRITE_NOFLAG;
    size_t out_len = 0;
    char *json = yyjson_mut_write_opts(doc, flg, NULL, &out_len, NULL);

    if (json) {
        lua_pushlstring(L, json, out_len);
        free(json);
    } else {
        lua_pushnil(L);
    }

    yyjson_mut_doc_free(doc);
    return 1;
}

int luaopen_json(lua_State *L) {
    lua_pushcfunction(L, l_json_parse);
    lua_setglobal(L, "json_parse");

    lua_pushcfunction(L, l_json_stringify);
    lua_setglobal(L, "json_stringify");

    return 0;
}
