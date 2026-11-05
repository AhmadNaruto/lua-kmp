package com.seanproctor.lua

public data class LuaConfig(
    /** Which standard libraries to open. Default = a safe subset (no io, no os, no package, no debug). */
    val stdlibs: Set<StdLib> = StdLib.SAFE_DEFAULT,
)
