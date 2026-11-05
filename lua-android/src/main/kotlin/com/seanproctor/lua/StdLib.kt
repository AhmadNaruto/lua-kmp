package com.seanproctor.lua

/**
 * The Lua 5.5 standard libraries, for [LuaConfig.stdlibs] sandbox selection.
 * Selected libraries are opened with `luaL_openselectedlibs`; nothing else is
 * ever loaded into the state.
 */
public enum class StdLib {
    BASE, COROUTINE, TABLE, STRING, MATH, UTF8, IO, OS, PACKAGE, DEBUG;

    public companion object {
        public val ALL: Set<StdLib> = entries.toSet()

        /**
         * Safe for running untrusted-ish scripts: everything except
         * [IO], [OS], [PACKAGE], and [DEBUG].
         */
        public val SAFE_DEFAULT: Set<StdLib> = setOf(BASE, COROUTINE, TABLE, STRING, MATH, UTF8)
    }
}
