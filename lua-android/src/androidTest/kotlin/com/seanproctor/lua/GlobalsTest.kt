package com.seanproctor.lua

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs

class GlobalsTest {

    @Test
    fun absentGlobalIsNil() {
        LuaState().use { lua ->
            assertEquals(LuaValue.Nil, lua.getGlobal("nope"))
        }
    }

    @Test
    fun primitiveGlobalsRoundTrip() {
        LuaState().use { lua ->
            lua.setGlobal("b", LuaValue.Bool(true))
            lua.setGlobal("i", LuaValue.Integer(7))
            lua.setGlobal("n", LuaValue.Number(1.25))
            lua.setGlobal("s", LuaValue.Str("str"))
            assertEquals(LuaValue.Bool(true), lua.getGlobal("b"))
            assertEquals(LuaValue.Integer(7), lua.getGlobal("i"))
            assertEquals(LuaValue.Number(1.25), lua.getGlobal("n"))
            assertEquals(LuaValue.Str("str"), lua.getGlobal("s"))
            assertEquals(
                listOf(LuaValue.Bool(true), LuaValue.Integer(7), LuaValue.Number(1.25), LuaValue.Str("str")),
                lua.eval("return b, i, n, s"),
            )
        }
    }

    @Test
    fun settingNilClearsGlobal() {
        LuaState().use { lua ->
            lua.setGlobal("x", LuaValue.Integer(1))
            lua.setGlobal("x", LuaValue.Nil)
            assertEquals(LuaValue.Nil, lua.getGlobal("x"))
        }
    }

    @Test
    fun tableGlobalRoundTrips() {
        LuaState().use { lua ->
            lua.eval("t = {answer = 42}")
            val t = lua.getGlobal("t")
            assertIs<LuaValue.Table>(t)
            assertEquals(LuaValue.Integer(42), t[LuaValue.Str("answer")])

            // Put the same handle back under another name; Lua sees one table.
            lua.setGlobal("u", t)
            assertEquals(listOf<LuaValue>(LuaValue.Bool(true)), lua.eval("return t == u"))
        }
    }

    @Test
    fun globalsAreIndependentBetweenStates() {
        LuaState().use { a ->
            LuaState().use { b ->
                a.setGlobal("x", LuaValue.Integer(1))
                assertEquals(LuaValue.Nil, b.getGlobal("x"))
            }
        }
    }
}
