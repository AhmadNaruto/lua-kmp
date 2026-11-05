package com.seanproctor.lua.internal

import com.seanproctor.lua.LuaState

internal fun currentThreadToken(): Any = Thread.currentThread()

/**
 * State shared by both backends: thread confinement, the closed flag, and the
 * host-function registry.
 */
internal abstract class BaseLuaState : LuaState {
    private val ownerThread: Any = currentThreadToken()

    internal var closed: Boolean = false
        private set

    internal val handlers = HandlerRegistry()

    protected fun markClosed() {
        closed = true
    }

    internal fun checkOwnerThread() {
        check(currentThreadToken() === ownerThread) {
            "A LuaState is confined to the thread that created it and may not be used from another thread"
        }
    }

    internal fun checkOpen() {
        checkOwnerThread()
        check(!closed) { "LuaState has been closed" }
    }
}
