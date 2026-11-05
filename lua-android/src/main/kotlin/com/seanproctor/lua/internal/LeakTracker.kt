package com.seanproctor.lua.internal

import kotlin.concurrent.atomics.AtomicInt
import kotlin.concurrent.atomics.ExperimentalAtomicApi

/**
 * Debug counters proving the lifetime contract: every pinned cross-boundary
 * reference (a `StableRef` on native, a state-registry entry on the JVM) and
 * every registered handler must be released by `close()`. The leak tests in
 * commonTest assert these return to their baseline after create/close loops.
 */
@OptIn(ExperimentalAtomicApi::class)
internal object LeakTracker {
    private val pinned = AtomicInt(0)
    private val handlers = AtomicInt(0)

    val pinnedCount: Int get() = pinned.load()
    val handlerCount: Int get() = handlers.load()

    fun statePinned() {
        pinned.addAndFetch(1)
    }

    fun stateReleased() {
        pinned.addAndFetch(-1)
    }

    fun handlerAdded() {
        handlers.addAndFetch(1)
    }

    fun handlersRemoved(count: Int) {
        handlers.addAndFetch(-count)
    }
}
