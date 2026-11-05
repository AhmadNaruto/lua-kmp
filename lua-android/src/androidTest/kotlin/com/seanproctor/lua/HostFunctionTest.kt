package com.seanproctor.lua

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class HostFunctionTest {

    @Test
    fun registeredFunctionIsCallableFromLua() {
        LuaState().use { lua ->
            lua.register("ktAdd") { args ->
                listOf(LuaValue.Integer(args.sumOf { (it as LuaValue.Integer).value }))
            }
            assertEquals(listOf<LuaValue>(LuaValue.Integer(6)), lua.eval("return ktAdd(1, 2, 3)"))
        }
    }

    @Test
    fun hostFunctionReceivesMarshalledPrimitives() {
        LuaState().use { lua ->
            var received: List<LuaValue>? = null
            lua.register("capture") { args ->
                received = args
                emptyList()
            }
            lua.eval("capture(nil, true, 7, 0.5, 'x')")
            assertEquals(
                listOf(LuaValue.Nil, LuaValue.Bool(true), LuaValue.Integer(7), LuaValue.Number(0.5), LuaValue.Str("x")),
                received,
            )
        }
    }

    @Test
    fun hostFunctionReturnsMultipleValues() {
        LuaState().use { lua ->
            lua.register("pair") { args ->
                listOf(args.first(), args.first())
            }
            assertEquals(
                listOf<LuaValue>(LuaValue.Integer(9), LuaValue.Integer(9)),
                lua.eval("return pair(9)"),
            )
        }
    }

    @Test
    fun hostFunctionAcceptsAndReturnsTables() {
        LuaState().use { lua ->
            lua.register("bump") { args ->
                val t = args.single()
                assertIs<LuaValue.Table>(t)
                val n = t[LuaValue.Str("n")]
                assertIs<LuaValue.Integer>(n)
                t[LuaValue.Str("n")] = LuaValue.Integer(n.value + 1)
                listOf(t)
            }
            assertEquals(
                listOf(LuaValue.Bool(true), LuaValue.Integer(2)),
                lua.eval("local t = {n = 1} ; local r = bump(t) ; return r == t, r.n"),
            )
        }
    }

    @Test
    fun wrappedFunctionCanLiveInsideATable() {
        LuaState().use { lua ->
            lua.eval("api = {}")
            val api = lua.getGlobal("api")
            assertIs<LuaValue.Table>(api)
            api[LuaValue.Str("double")] = lua.wrap { args ->
                listOf(LuaValue.Integer((args.single() as LuaValue.Integer).value * 2))
            }
            assertEquals(listOf<LuaValue>(LuaValue.Integer(14)), lua.eval("return api.double(7)"))
        }
    }

    @Test
    fun wrappedFunctionIsCallableViaCall() {
        LuaState().use { lua ->
            val f = lua.wrap { args -> args.reversed() }
            assertEquals(
                listOf<LuaValue>(LuaValue.Integer(2), LuaValue.Integer(1)),
                lua.call(f, listOf(LuaValue.Integer(1), LuaValue.Integer(2))),
            )
        }
    }

    @Test
    fun hostFunctionWorksInsideCoroutines() {
        LuaState().use { lua ->
            lua.register("hostDouble") { args ->
                listOf(LuaValue.Integer((args.single() as LuaValue.Integer).value * 2))
            }
            assertEquals(
                listOf<LuaValue>(LuaValue.Integer(20)),
                lua.eval(
                    """
                    local co = coroutine.wrap(function(x) coroutine.yield() ; return hostDouble(x) end)
                    co(10)
                    return co()
                    """.trimIndent()
                ),
            )
        }
    }

    @Test
    fun hostExceptionBecomesLuaErrorCatchableByScript() {
        LuaState().use { lua ->
            lua.register("angry") { _ -> throw IllegalStateException("kotlin says no") }
            val results = lua.eval("local ok, err = pcall(angry) ; return ok, err")
            assertEquals(LuaValue.Bool(false), results[0])
            val message = results[1]
            assertIs<LuaValue.Str>(message)
            assertTrue("kotlin says no" in message.value, "unexpected message: ${message.value}")
        }
    }

    @Test
    fun uncaughtHostExceptionSurfacesAsLuaRuntimeError() {
        LuaState().use { lua ->
            lua.register("angry") { _ -> throw IllegalStateException("kotlin says no") }
            val e = assertFailsWith<LuaRuntimeError> { lua.eval("angry()") }
            assertTrue("kotlin says no" in e.message!!, "unexpected message: ${e.message}")
        }
    }

    @Test
    fun luaContinuesAfterCaughtHostError() {
        LuaState().use { lua ->
            var calls = 0
            lua.register("flaky") { _ ->
                calls++
                if (calls == 1) throw RuntimeException("first call fails") else listOf(LuaValue.Integer(99))
            }
            assertEquals(
                listOf(LuaValue.Bool(false), LuaValue.Integer(99)),
                lua.eval("local ok = pcall(flaky) ; return ok, flaky()"),
            )
        }
    }
}
