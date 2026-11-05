package com.seanproctor.lua.internal

import com.seanproctor.lua.LuaException
import com.seanproctor.lua.LuaMemoryError
import com.seanproctor.lua.LuaRuntimeError
import com.seanproctor.lua.LuaSyntaxError
import com.seanproctor.lua.StdLib

/*
 * Lua status codes and type tags, mirrored from lua.h. These have been stable
 * across all of Lua 5.x; the C shim is compiled against the vendored headers,
 * so any upstream change would surface as a test failure, not silent skew.
 */
internal const val LUA_STATUS_OK: Int = 0
internal const val LUA_STATUS_ERRRUN: Int = 2
internal const val LUA_STATUS_ERRSYNTAX: Int = 3
internal const val LUA_STATUS_ERRMEM: Int = 4

internal const val LUA_TYPE_NONE: Int = -1
internal const val LUA_TYPE_NIL: Int = 0
internal const val LUA_TYPE_BOOLEAN: Int = 1
internal const val LUA_TYPE_LIGHTUSERDATA: Int = 2
internal const val LUA_TYPE_NUMBER: Int = 3
internal const val LUA_TYPE_STRING: Int = 4
internal const val LUA_TYPE_TABLE: Int = 5
internal const val LUA_TYPE_FUNCTION: Int = 6
internal const val LUA_TYPE_USERDATA: Int = 7
internal const val LUA_TYPE_THREAD: Int = 8

internal const val LUA_MULTIPLE_RETURNS: Int = -1

internal fun luaErrorFor(status: Int, message: String): LuaException = when (status) {
    LUA_STATUS_ERRSYNTAX -> LuaSyntaxError(message)
    LUA_STATUS_ERRMEM -> LuaMemoryError(message)
    else -> LuaRuntimeError(message)
}

internal fun luaTypeName(type: Int): String = when (type) {
    LUA_TYPE_NONE -> "none"
    LUA_TYPE_NIL -> "nil"
    LUA_TYPE_BOOLEAN -> "boolean"
    LUA_TYPE_LIGHTUSERDATA, LUA_TYPE_USERDATA -> "userdata"
    LUA_TYPE_NUMBER -> "number"
    LUA_TYPE_STRING -> "string"
    LUA_TYPE_TABLE -> "table"
    LUA_TYPE_FUNCTION -> "function"
    LUA_TYPE_THREAD -> "thread"
    else -> "unknown($type)"
}

/* Must match the KL_LIB_* bits in native/shim/lua_shim.h. */
internal val StdLib.maskBit: Int
    get() = when (this) {
        StdLib.BASE -> 1
        StdLib.COROUTINE -> 2
        StdLib.TABLE -> 4
        StdLib.STRING -> 8
        StdLib.MATH -> 16
        StdLib.UTF8 -> 32
        StdLib.IO -> 64
        StdLib.OS -> 128
        StdLib.PACKAGE -> 256
        StdLib.DEBUG -> 512
    }

internal fun Set<StdLib>.toMask(): Int = fold(0) { acc, lib -> acc or lib.maskBit }
