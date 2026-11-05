package com.seanproctor.lua

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertTrue

class EvalTest {

    @Test
    fun evalReturnsInteger() {
        LuaState().use { lua ->
            assertEquals(listOf<LuaValue>(LuaValue.Integer(2)), lua.eval("return 1 + 1"))
        }
    }

    @Test
    fun evalWithNoResultsReturnsEmptyList() {
        LuaState().use { lua ->
            assertEquals(emptyList(), lua.eval("local x = 1"))
        }
    }

    @Test
    fun evalReturnsMultipleValues() {
        LuaState().use { lua ->
            assertEquals(
                listOf(LuaValue.Integer(1), LuaValue.Str("two"), LuaValue.Bool(true)),
                lua.eval("return 1, 'two', true"),
            )
        }
    }

    @Test
    fun primitiveRoundTrips() {
        LuaState().use { lua ->
            assertEquals(
                listOf(
                    LuaValue.Nil,
                    LuaValue.Bool(false),
                    LuaValue.Integer(42),
                    LuaValue.Number(2.5),
                    LuaValue.Str("hello"),
                ),
                lua.eval("return nil, false, 42, 2.5, 'hello'"),
            )
        }
    }

    @Test
    fun integerAndFloatSubtypesArePreserved() {
        LuaState().use { lua ->
            assertEquals(
                listOf(LuaValue.Integer(3), LuaValue.Number(3.0)),
                lua.eval("return 3, 3.0"),
            )
            // Lua integer division of integers stays integer; / always floats.
            assertEquals(
                listOf(LuaValue.Integer(2), LuaValue.Number(2.0)),
                lua.eval("return 4 // 2, 4 / 2"),
            )
        }
    }

    @Test
    fun longRangeIntegersSurvive() {
        LuaState().use { lua ->
            assertEquals(
                listOf<LuaValue>(LuaValue.Integer(Long.MAX_VALUE)),
                lua.eval("return math.maxinteger"),
            )
            assertEquals(
                listOf<LuaValue>(LuaValue.Integer(Long.MIN_VALUE)),
                lua.eval("return math.mininteger"),
            )
        }
    }

    @Test
    fun unicodeStringsRoundTrip() {
        LuaState().use { lua ->
            val results = lua.eval("return 'héllo — 世界 🌙'")
            assertEquals(listOf<LuaValue>(LuaValue.Str("héllo — 世界 🌙")), results)
        }
    }

    @Test
    fun luaIsVersion55() {
        LuaState().use { lua ->
            val version = lua.eval("return _VERSION").single()
            assertIs<LuaValue.Str>(version)
            assertTrue(version.value.contains("5.5"), "expected Lua 5.5, got ${version.value}")
        }
    }

    @Test
    fun evalResultsSurviveAcrossCalls() {
        LuaState().use { lua ->
            val table = lua.eval("return {10, 20}").single()
            assertIs<LuaValue.Table>(table)
            lua.eval("collectgarbage('collect')")
            assertEquals(LuaValue.Integer(10), table[LuaValue.Integer(1)])
        }
    }
}
