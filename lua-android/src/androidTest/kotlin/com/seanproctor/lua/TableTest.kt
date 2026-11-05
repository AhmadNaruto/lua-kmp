package com.seanproctor.lua

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertFailsWith

class TableTest {

    private fun LuaState.table(code: String): LuaValue.Table {
        val value = eval(code).single()
        assertIs<LuaValue.Table>(value)
        return value
    }

    @Test
    fun sequenceSizeAndIndexing() {
        LuaState().use { lua ->
            val t = lua.table("return {10, 20, 30}")
            assertEquals(3, t.size)
            assertEquals(LuaValue.Integer(10), t[LuaValue.Integer(1)])
            assertEquals(LuaValue.Integer(30), t[LuaValue.Integer(3)])
            assertEquals(LuaValue.Nil, t[LuaValue.Integer(4)])
        }
    }

    @Test
    fun writesAreVisibleToLua() {
        LuaState().use { lua ->
            lua.eval("t = {}")
            val t = lua.getGlobal("t")
            assertIs<LuaValue.Table>(t)
            t[LuaValue.Str("name")] = LuaValue.Str("kotlin")
            t[LuaValue.Integer(1)] = LuaValue.Number(0.5)
            assertEquals(
                listOf(LuaValue.Str("kotlin"), LuaValue.Number(0.5)),
                lua.eval("return t.name, t[1]"),
            )
        }
    }

    @Test
    fun luaWritesAreVisibleThroughHandle() {
        LuaState().use { lua ->
            val t = lua.table("t = {} ; return t")
            lua.eval("t.x = 99")
            assertEquals(LuaValue.Integer(99), t[LuaValue.Str("x")])
        }
    }

    @Test
    fun toMapCopiesAllEntries() {
        LuaState().use { lua ->
            val t = lua.table("return {a = 1, [2] = 'two', [2.5] = true}")
            val map = t.toMap()
            assertEquals(3, map.size)
            assertEquals(LuaValue.Integer(1), map[LuaValue.Str("a")])
            assertEquals(LuaValue.Str("two"), map[LuaValue.Integer(2)])
            assertEquals(LuaValue.Bool(true), map[LuaValue.Number(2.5)])
        }
    }

    @Test
    fun nestedTablesComeBackAsHandles() {
        LuaState().use { lua ->
            val t = lua.table("return {inner = {5}}")
            val inner = t[LuaValue.Str("inner")]
            assertIs<LuaValue.Table>(inner)
            assertEquals(LuaValue.Integer(5), inner[LuaValue.Integer(1)])
        }
    }

    @Test
    fun sizeHonorsLenMetamethod() {
        LuaState().use { lua ->
            val t = lua.table("return setmetatable({}, {__len = function() return 12 end})")
            assertEquals(12, t.size)
        }
    }

    @Test
    fun metamethodErrorsSurfaceAsLuaRuntimeError() {
        LuaState().use { lua ->
            val t = lua.table("return setmetatable({}, {__index = function() error('trap') end})")
            val e = assertFailsWith<LuaRuntimeError> { t[LuaValue.Str("anything")] }
            check(e.message!!.contains("trap"))
        }
    }

    @Test
    fun tableHandleFromAnotherStateIsRejected() {
        LuaState().use { a ->
            LuaState().use { b ->
                val t = a.table("return {}")
                assertFailsWith<IllegalArgumentException> { b.setGlobal("t", t) }
            }
        }
    }
}
