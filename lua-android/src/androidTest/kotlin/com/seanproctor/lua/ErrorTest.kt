package com.seanproctor.lua

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class ErrorTest {

    @Test
    fun syntaxErrorThrowsLuaSyntaxError() {
        LuaState().use { lua ->
            assertFailsWith<LuaSyntaxError> { lua.eval("return ++ 1") }
            assertFailsWith<LuaSyntaxError> { lua.load("local = 5") }
        }
    }

    @Test
    fun runtimeErrorThrowsLuaRuntimeErrorWithMessage() {
        LuaState().use { lua ->
            val e = assertFailsWith<LuaRuntimeError> { lua.eval("error('bang')") }
            assertTrue("bang" in e.message!!, "unexpected message: ${e.message}")
        }
    }

    @Test
    fun nonStringErrorObjectStillThrows() {
        LuaState().use { lua ->
            assertFailsWith<LuaRuntimeError> { lua.eval("error({code = 7})") }
        }
    }

    @Test
    fun chunkNameAppearsInErrorMessages() {
        LuaState().use { lua ->
            val e = assertFailsWith<LuaRuntimeError> {
                lua.eval("error('x')", chunkName = "=myChunk")
            }
            assertTrue("myChunk" in e.message!!, "unexpected message: ${e.message}")
        }
    }

    @Test
    fun stateStaysUsableAfterErrors() {
        LuaState().use { lua ->
            assertFailsWith<LuaRuntimeError> { lua.eval("error('first')") }
            assertFailsWith<LuaSyntaxError> { lua.eval("not valid lua") }
            assertEquals(listOf<LuaValue>(LuaValue.Integer(3)), lua.eval("return 3"))
        }
    }

    @Test
    fun usingClosedStateThrows() {
        val lua = LuaState()
        lua.close()
        assertFailsWith<IllegalStateException> { lua.eval("return 1") }
        assertFailsWith<IllegalStateException> { lua.getGlobal("x") }
    }

    @Test
    fun closeIsIdempotent() {
        val lua = LuaState()
        lua.close()
        lua.close()
    }

    @Test
    fun handlesAreDeadAfterClose() {
        val lua = LuaState()
        val t = lua.eval("return {1}").single() as LuaValue.Table
        lua.close()
        assertFailsWith<IllegalStateException> { t[LuaValue.Integer(1)] }
        assertFailsWith<IllegalStateException> { t.size }
    }
}
