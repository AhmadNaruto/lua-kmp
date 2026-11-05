package com.seanproctor.lua.internal

import com.seanproctor.lua.LuaFunction

/**
 * Maps the integer id carried in a dispatch closure's upvalue to the Kotlin
 * handler it stands for. One registry per state; cleared on close.
 */
internal class HandlerRegistry {
    private val handlers = HashMap<Int, LuaFunction>()
    private var nextId = 1

    val size: Int get() = handlers.size

    fun add(function: LuaFunction): Int {
        val id = nextId++
        handlers[id] = function
        LeakTracker.handlerAdded()
        return id
    }

    fun get(id: Int): LuaFunction? = handlers[id]

    fun clear() {
        LeakTracker.handlersRemoved(handlers.size)
        handlers.clear()
    }
}
