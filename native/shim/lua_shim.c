#include "lua_shim.h"
#include "lua_html.h"
#include "lua_pcre.h"
#include "lua_json.h"
#include "lua_crypto.h"
#include "lua_utils.h"
#include <stdlib.h>
#include <string.h>

void kl_openlibs(lua_State *L, int mask) {
  int load = 0;
  if (mask & KL_LIB_BASE)      load |= LUA_GLIBK;
  if (mask & KL_LIB_COROUTINE) load |= LUA_COLIBK;
  if (mask & KL_LIB_TABLE)     load |= LUA_TABLIBK;
  if (mask & KL_LIB_STRING)    load |= LUA_STRLIBK;
  if (mask & KL_LIB_MATH)      load |= LUA_MATHLIBK;
  if (mask & KL_LIB_UTF8)      load |= LUA_UTF8LIBK;
  if (mask & KL_LIB_IO)        load |= LUA_IOLIBK;
  if (mask & KL_LIB_OS)        load |= LUA_OSLIBK;
  if (mask & KL_LIB_PACKAGE)   load |= LUA_LOADLIBK;
  if (mask & KL_LIB_DEBUG)     load |= LUA_DBLIBK;
  luaL_openselectedlibs(L, load, 0);

  /* Open NoveLA native modules */
  luaopen_html(L);
  luaopen_pcre(L);
  luaopen_json(L);
  luaopen_crypto(L);
  luaopen_utils(L);
}

int kl_pcall(lua_State *L, int nargs, int nresults) {
  return lua_pcall(L, nargs, nresults, 0);
}

void kl_pop(lua_State *L, int n) {
  lua_pop(L, n);
}

int kl_upvalue_int(lua_State *L, int n) {
  return (int)lua_tointeger(L, lua_upvalueindex(n));
}

const char *kl_tolstring(lua_State *L, int idx, int *len_out) {
  size_t len = 0;
  const char *s = lua_tolstring(L, idx, &len);
  if (len_out)
    *len_out = (int)len;
  return s;
}

void kl_pushlstring(lua_State *L, const char *s, int len) {
  lua_pushlstring(L, s, (size_t)len);
}

int kl_loadbuffer(lua_State *L, const char *buf, int len, const char *name) {
  /* "bt": accept both text and precompiled binary bytecode chunks (.lbc) */
  return luaL_loadbufferx(L, buf, (size_t)len, name, "bt");
}

typedef struct {
  char  *data;
  size_t size;
  size_t capacity;
} kl_dump_buf_t;

static int kl_dump_writer(lua_State *L, const void *p, size_t sz, void *ud) {
  (void)L;
  kl_dump_buf_t *b = (kl_dump_buf_t *)ud;
  if (b->size + sz > b->capacity) {
    size_t new_cap = (b->capacity == 0) ? 1024 : b->capacity * 2;
    while (new_cap < b->size + sz) new_cap *= 2;
    char *new_data = (char *)realloc(b->data, new_cap);
    if (!new_data) return 1;
    b->data = new_data;
    b->capacity = new_cap;
  }
  memcpy(b->data + b->size, p, sz);
  b->size += sz;
  return 0;
}

char *kl_dump(lua_State *L, int *len_out) {
  kl_dump_buf_t b = {NULL, 0, 0};
  if (lua_dump(L, kl_dump_writer, &b, 0) == 0) {
    if (len_out) *len_out = (int)b.size;
    return b.data;
  }
  if (b.data) free(b.data);
  if (len_out) *len_out = 0;
  return NULL;
}

int kl_ref(lua_State *L) {
  return luaL_ref(L, LUA_REGISTRYINDEX);
}

void kl_unref(lua_State *L, int ref) {
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

int kl_getref(lua_State *L, int ref) {
  return lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
}

void kl_setextra(lua_State *L, void *p) {
  *(void **)lua_getextraspace(L) = p;
}

void *kl_getextra(lua_State *L) {
  return *(void **)lua_getextraspace(L);
}

static kl_callback kl_host_callback = NULL;

void kl_set_callback(kl_callback cb) {
  kl_host_callback = cb;
}

/*
** The single lua_CFunction behind every host function. The Kotlin (or JNI)
** callback marshals arguments, runs the handler, and pushes results; it must
** return normally even when the handler fails, reporting the failure as a
** negative count with the message on top. Only then does this frame raise
** the Lua error, so the longjmp never crosses a Kotlin or JNI frame.
*/
static int kl_dispatch(lua_State *L) {
  int nresults;
  if (kl_host_callback == NULL) {
    lua_pushliteral(L, "lua-kmp: host callback not installed");
    return lua_error(L);
  }
  nresults = kl_host_callback(L);
  if (nresults < 0)
    return lua_error(L); /* message pushed by the callback */
  return nresults;
}

void kl_push_dispatch_closure(lua_State *L, int id) {
  lua_pushinteger(L, id);
  lua_pushcclosure(L, kl_dispatch, 1);
}

static int gettable_aux(lua_State *L) {
  lua_gettable(L, 1);
  return 1;
}

int kl_pgettable(lua_State *L) {
  lua_pushcfunction(L, gettable_aux);
  lua_insert(L, -3); /* [.., aux, table, key] */
  return lua_pcall(L, 2, 1, 0);
}

static int settable_aux(lua_State *L) {
  lua_settable(L, 1);
  return 0;
}

int kl_psettable(lua_State *L) {
  lua_pushcfunction(L, settable_aux);
  lua_insert(L, -4); /* [.., aux, table, key, value] */
  return lua_pcall(L, 3, 0, 0);
}

static int len_aux(lua_State *L) {
  lua_pushinteger(L, luaL_len(L, 1));
  return 1;
}

int kl_plen(lua_State *L) {
  lua_pushcfunction(L, len_aux);
  lua_insert(L, -2); /* [.., aux, table] */
  return lua_pcall(L, 1, 1, 0);
}
