package com.seanproctor.lua

import com.seanproctor.lua.internal.LeakTracker
import java.util.concurrent.ConcurrentHashMap

/**
 * Tracks live states by their lua_State pointer so the dispatch upcall can
 * find the owner. The C side passes the main state's pointer (recovered from
 * the extraspace, so it is correct even when a coroutine thread calls).
 */
internal object JniStateRegistry {
    private val states = ConcurrentHashMap<Long, JniLuaState>()

    fun add(ptr: Long, state: JniLuaState) {
        states[ptr] = state
        LeakTracker.statePinned()
    }

    fun remove(ptr: Long) {
        if (states.remove(ptr) != null) LeakTracker.stateReleased()
    }

    fun find(ptr: Long): JniLuaState? = states[ptr]
}

/**
 * The raw JNI surface (native/jni/lua_jni.c). All strings cross the boundary
 * as UTF-8 byte arrays to avoid JNI's modified-UTF-8. State handles are the
 * lua_State* as a Long.
 */
internal object LuaJni {

    init {
        loadLuaNative()
    }

    external fun newState(): Long
    external fun openLibs(state: Long, mask: Int)
    external fun close(state: Long)
    external fun loadBuffer(state: Long, code: ByteArray, chunkName: ByteArray): Int
    external fun dump(state: Long): ByteArray?
    external fun pcall(state: Long, nargs: Int, nresults: Int): Int
    external fun getTop(state: Long): Int
    external fun setTop(state: Long, idx: Int)
    external fun checkStack(state: Long, n: Int): Boolean
    external fun pushNil(state: Long)
    external fun pushBoolean(state: Long, value: Boolean)
    external fun pushInteger(state: Long, value: Long)
    external fun pushNumber(state: Long, value: Double)
    external fun pushString(state: Long, bytes: ByteArray)
    external fun pushValue(state: Long, idx: Int)
    external fun pushClosure(state: Long, id: Int)
    external fun type(state: Long, idx: Int): Int
    external fun isInteger(state: Long, idx: Int): Boolean
    external fun toBoolean(state: Long, idx: Int): Boolean
    external fun toInteger(state: Long, idx: Int): Long
    external fun toNumber(state: Long, idx: Int): Double
    external fun toStringBytes(state: Long, idx: Int): ByteArray?
    external fun newTable(state: Long)
    external fun next(state: Long, idx: Int): Int
    external fun getGlobal(state: Long, name: ByteArray): Int
    external fun setGlobal(state: Long, name: ByteArray)
    external fun ref(state: Long): Int
    external fun unref(state: Long, ref: Int)
    external fun getRef(state: Long, ref: Int): Int
    external fun pGetTable(state: Long): Int
    external fun pSetTable(state: Long): Int
    external fun pLen(state: Long): Int

    /**
     * Called from the C dispatcher (jni_host_callback) for every host-function
     * invocation. Mirrors the native backend's dispatchHost: marshal args off
     * the calling thread's stack, run the handler, push results, and return
     * the result count — or push an error message and return -1, letting the
     * C side raise the Lua error after this frame has returned. Must never
     * throw across JNI.
     */
    @JvmStatic
    fun nativeCallback(threadPtr: Long, mainPtr: Long, id: Int): Int {
        return try {
            val state = JniStateRegistry.find(mainPtr)
                ?: throw LuaRuntimeError("lua-kmp: no owning state for host callback")
            val handler = state.handlers.get(id)
                ?: throw LuaRuntimeError("lua-kmp: unknown host function id $id")
            val nargs = getTop(threadPtr)
            val args = ArrayList<LuaValue>(nargs)
            for (i in 1..nargs) args.add(state.marshal(threadPtr, i))
            val results = handler.invoke(args)
            checkStack(threadPtr, results.size + 1)
            for (result in results) state.push(threadPtr, result)
            results.size
        } catch (t: Throwable) {
            try {
                pushString(threadPtr, (t.message ?: "host function error").encodeToByteArray())
            } catch (_: Throwable) {
                // Nothing safer to do; the C side pushes a generic message if needed.
            }
            -1
        }
    }
}
