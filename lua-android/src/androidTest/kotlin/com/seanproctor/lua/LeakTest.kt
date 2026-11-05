package com.seanproctor.lua

import com.seanproctor.lua.internal.LeakTracker
import kotlin.test.Test
import kotlin.test.assertEquals

class LeakTest {

    @Test
    fun pinnedReferencesAreReleasedByClose() {
        val pinnedBaseline = LeakTracker.pinnedCount
        val handlerBaseline = LeakTracker.handlerCount

        repeat(50) { round ->
            LuaState().use { lua ->
                repeat(5) { i ->
                    lua.register("fn$i") { args -> args }
                }
                repeat(3) {
                    lua.wrap { args -> args }
                }
                // Table/function handles pin registry refs until close.
                val t = lua.eval("return {round = $round}").single() as LuaValue.Table
                assertEquals(LuaValue.Integer(round.toLong()), t[LuaValue.Str("round")])
                lua.eval("return fn0(1), fn4('x')")
            }
        }

        assertEquals(pinnedBaseline, LeakTracker.pinnedCount, "pinned references leaked")
        assertEquals(handlerBaseline, LeakTracker.handlerCount, "handler registry entries leaked")
    }

    @Test
    fun closeReleasesEvenAfterErrors() {
        val pinnedBaseline = LeakTracker.pinnedCount
        val handlerBaseline = LeakTracker.handlerCount

        repeat(20) {
            val lua = LuaState()
            lua.register("bad") { _ -> throw RuntimeException("boom") }
            try {
                lua.eval("bad()")
            } catch (_: LuaRuntimeError) {
                // expected
            }
            lua.close()
        }

        assertEquals(pinnedBaseline, LeakTracker.pinnedCount, "pinned references leaked")
        assertEquals(handlerBaseline, LeakTracker.handlerCount, "handler registry entries leaked")
    }
}
