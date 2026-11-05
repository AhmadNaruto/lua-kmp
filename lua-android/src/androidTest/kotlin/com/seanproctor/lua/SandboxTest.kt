package com.seanproctor.lua

import kotlin.test.Test
import kotlin.test.assertEquals

class SandboxTest {

    @Test
    fun safeDefaultOmitsDangerousLibraries() {
        LuaState().use { lua ->
            assertEquals(LuaValue.Nil, lua.getGlobal("os"))
            assertEquals(LuaValue.Nil, lua.getGlobal("io"))
            assertEquals(LuaValue.Nil, lua.getGlobal("package"))
            assertEquals(LuaValue.Nil, lua.getGlobal("debug"))
            // require comes from the package library, so it is absent too.
            assertEquals(LuaValue.Nil, lua.getGlobal("require"))
        }
    }

    @Test
    fun safeDefaultKeepsTheSafeLibraries() {
        LuaState().use { lua ->
            assertEquals(
                List(5) { LuaValue.Str("table") },
                lua.eval("return type(table), type(string), type(math), type(utf8), type(coroutine)"),
            )
            assertEquals(listOf<LuaValue>(LuaValue.Integer(3)), lua.eval("return math.floor(3.7)"))
            assertEquals(listOf<LuaValue>(LuaValue.Str("AB")), lua.eval("return ('ab'):upper()"))
        }
    }

    @Test
    fun allLibrariesOpensEverything() {
        LuaState(LuaConfig(stdlibs = StdLib.ALL)).use { lua ->
            assertEquals(
                listOf(LuaValue.Str("table"), LuaValue.Str("table"), LuaValue.Str("table"), LuaValue.Str("table")),
                lua.eval("return type(os), type(io), type(package), type(debug)"),
            )
        }
    }

    @Test
    fun explicitSubsetOpensOnlyThatSubset() {
        LuaState(LuaConfig(stdlibs = setOf(StdLib.BASE, StdLib.MATH))).use { lua ->
            assertEquals(listOf<LuaValue>(LuaValue.Integer(1)), lua.eval("return math.floor(1.9)"))
            assertEquals(LuaValue.Nil, lua.getGlobal("string"))
            assertEquals(LuaValue.Nil, lua.getGlobal("table"))
            assertEquals(LuaValue.Nil, lua.getGlobal("coroutine"))
        }
    }
}
