/*
** Thin C shim over the Lua C API for the lua-kmp bindings.
**
** Two jobs:
**   1. Re-expose the function-like macros the bindings need as real,
**      externally linkable functions (cinterop cannot bind macros).
**   2. Provide small helpers shared by the cinterop and JNI backends:
**      registry refs, extraspace access, host-callback dispatch, protected
**      table operations, and standard-library selection.
**
** Keep this surface minimal: only wrap what the bindings actually call.
*/
#ifndef LUA_SHIM_H
#define LUA_SHIM_H

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/*
** Standard-library selection mask. Mirrored by com.seanproctor.lua.StdLib;
** the bit order must match StdLib.maskBit exactly.
*/
#define KL_LIB_BASE      1
#define KL_LIB_COROUTINE 2
#define KL_LIB_TABLE     4
#define KL_LIB_STRING    8
#define KL_LIB_MATH      16
#define KL_LIB_UTF8      32
#define KL_LIB_IO        64
#define KL_LIB_OS        128
#define KL_LIB_PACKAGE   256
#define KL_LIB_DEBUG     512

/*
** Host-function callback. Invoked by the dispatcher with the calling
** lua_State (which may be a coroutine thread). Returns the number of results
** pushed onto the stack, or a negative value with an error message pushed on
** top, in which case the dispatcher raises a Lua error after the callback
** has returned (so the error never unwinds across the callback's frames).
*/
typedef int (*kl_callback)(lua_State *L);

/* Opens the standard libraries selected by mask (KL_LIB_* bits). */
void        kl_openlibs(lua_State *L, int mask);

/* Macro wrappers. */
int         kl_pcall(lua_State *L, int nargs, int nresults);
void        kl_pop(lua_State *L, int n);
int         kl_upvalue_int(lua_State *L, int n);

/*
** Length-explicit string helpers, with int lengths so the Kotlin side never
** deals in size_t. Lua strings can contain embedded NULs; lengths are always
** passed, never inferred. kl_tolstring wraps lua_tolstring: it returns NULL
** for values with no string representation and converts numbers in place on
** the stack. The returned pointer is only valid while the value stays on the
** stack: copy immediately.
*/
const char *kl_tolstring(lua_State *L, int idx, int *len_out);
void        kl_pushlstring(lua_State *L, const char *s, int len);
int         kl_loadbuffer(lua_State *L, const char *buf, int len, const char *name);
char       *kl_dump(lua_State *L, int *len_out);

/* Registry references (LUA_REGISTRYINDEX is a macro, so wrap the ref API). */
int         kl_ref(lua_State *L);
void        kl_unref(lua_State *L, int ref);
int         kl_getref(lua_State *L, int ref);

/*
** Extraspace access. lua-kmp stores one pointer identifying the owning
** binding state here; Lua copies it into every coroutine thread it creates,
** so the dispatcher can recover the owner from any calling thread.
*/
void        kl_setextra(lua_State *L, void *p);
void       *kl_getextra(lua_State *L);

/* Installs the process-wide host callback and pushes dispatch closures. */
void        kl_set_callback(kl_callback cb);
void        kl_push_dispatch_closure(lua_State *L, int id);

/*
** Protected table operations. Table access can trigger metamethods, which
** can raise; running them under lua_pcall keeps errors from longjmp-ing
** through the binding. Each returns a Lua status code; on error the message
** is on top of the stack.
**   kl_pgettable: [.., table, key] -> [.., value]
**   kl_psettable: [.., table, key, value] -> [..]
**   kl_plen:      [.., table] -> [.., length]
*/
int         kl_pgettable(lua_State *L);
int         kl_psettable(lua_State *L);
int         kl_plen(lua_State *L);

#endif
