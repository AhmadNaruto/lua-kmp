package com.seanproctor.lua

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertFailsWith

class CallTest {

    @Test
    fun loadReturnsCallableChunk() {
        LuaState().use { lua ->
            val chunk = lua.load("return 1 + 1")
            assertEquals(listOf<LuaValue>(LuaValue.Integer(2)), lua.call(chunk))
        }
    }

    @Test
    fun callLuaFunctionWithArgsAndResults() {
        LuaState().use { lua ->
            lua.eval("function add(a, b) return a + b end")
            val add = lua.getGlobal("add")
            assertIs<LuaValue.Function>(add)
            assertEquals(
                listOf<LuaValue>(LuaValue.Integer(5)),
                lua.call(add, listOf(LuaValue.Integer(2), LuaValue.Integer(3))),
            )
        }
    }

    @Test
    fun callReturnsMultipleResults() {
        LuaState().use { lua ->
            lua.eval("function pair(x) return x, x * 2 end")
            val pair = lua.getGlobal("pair")
            assertIs<LuaValue.Function>(pair)
            assertEquals(
                listOf<LuaValue>(LuaValue.Integer(4), LuaValue.Integer(8)),
                lua.call(pair, listOf(LuaValue.Integer(4))),
            )
        }
    }

    @Test
    fun callPassesTables() {
        LuaState().use { lua ->
            lua.eval("function sum(t) local s = 0 ; for _, v in ipairs(t) do s = s + v end ; return s end")
            val sum = lua.getGlobal("sum")
            assertIs<LuaValue.Function>(sum)
            val t = lua.eval("return {1, 2, 3, 4}").single()
            assertEquals(listOf<LuaValue>(LuaValue.Integer(10)), lua.call(sum, listOf(t)))
        }
    }

    @Test
    fun runtimeErrorInCalledFunctionIsMapped() {
        LuaState().use { lua ->
            lua.eval("function boom() error('exploded') end")
            val boom = lua.getGlobal("boom")
            assertIs<LuaValue.Function>(boom)
            val e = assertFailsWith<LuaRuntimeError> { lua.call(boom) }
            check(e.message!!.contains("exploded"))
        }
    }

    @Test
    fun functionHandleFromAnotherStateIsRejected() {
        LuaState().use { a ->
            LuaState().use { b ->
                val f = a.load("return 1")
                assertFailsWith<IllegalArgumentException> { b.call(f) }
            }
        }
    }
}
