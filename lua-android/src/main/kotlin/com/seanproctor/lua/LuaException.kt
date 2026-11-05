package com.seanproctor.lua

/** Base class for all errors reported by the Lua runtime. */
public open class LuaException(message: String) : RuntimeException(message)

/** A chunk failed to compile (load-time error). */
public class LuaSyntaxError(message: String) : LuaException(message)

/** A script or called function raised an error at run time. */
public class LuaRuntimeError(message: String) : LuaException(message)

/** The Lua allocator ran out of memory. */
public class LuaMemoryError(message: String) : LuaException(message)
