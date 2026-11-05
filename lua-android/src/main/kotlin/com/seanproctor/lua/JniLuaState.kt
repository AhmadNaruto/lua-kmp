package com.seanproctor.lua

import com.seanproctor.lua.internal.BaseLuaState
import com.seanproctor.lua.internal.LUA_MULTIPLE_RETURNS
import com.seanproctor.lua.internal.LUA_STATUS_OK
import com.seanproctor.lua.internal.LUA_TYPE_BOOLEAN
import com.seanproctor.lua.internal.LUA_TYPE_FUNCTION
import com.seanproctor.lua.internal.LUA_TYPE_LIGHTUSERDATA
import com.seanproctor.lua.internal.LUA_TYPE_NIL
import com.seanproctor.lua.internal.LUA_TYPE_NONE
import com.seanproctor.lua.internal.LUA_TYPE_NUMBER
import com.seanproctor.lua.internal.LUA_TYPE_STRING
import com.seanproctor.lua.internal.LUA_TYPE_TABLE
import com.seanproctor.lua.internal.LUA_TYPE_USERDATA
import com.seanproctor.lua.internal.luaErrorFor
import com.seanproctor.lua.internal.luaTypeName
import com.seanproctor.lua.internal.toMask

/**
 * The JVM/Android backend: same structure as NativeLuaState, with all
 * low-level operations going through the LuaJni externals on a lua_State*
 * handle held as a Long.
 */
internal class JniLuaState(config: LuaConfig) : BaseLuaState() {

    internal val ptr: Long

    init {
        val p = LuaJni.newState()
        if (p == 0L) throw LuaMemoryError("cannot allocate a new lua_State")
        ptr = p
        JniStateRegistry.add(p, this)
        LuaJni.openLibs(p, config.stdlibs.toMask())
    }

    override fun eval(code: String, chunkName: String): List<LuaValue> {
        checkOpen()
        val base = LuaJni.getTop(ptr)
        loadChunk(code.encodeToByteArray(), chunkName)
        return runPcall(base, nargs = 0)
    }

    override fun load(code: String, chunkName: String): LuaValue.Function =
        load(code.encodeToByteArray(), chunkName)

    override fun load(bytes: ByteArray, chunkName: String): LuaValue.Function {
        checkOpen()
        loadChunk(bytes, chunkName)
        return JniFunctionHandle(this, LuaJni.ref(ptr))
    }

    override fun dump(function: LuaValue.Function): ByteArray {
        checkOpen()
        val handle = function as? JniFunctionHandle
        require(handle != null && handle.owner === this) {
            "function handle was not created by this LuaState"
        }
        LuaJni.checkStack(ptr, 2)
        LuaJni.getRef(ptr, handle.ref)
        try {
            return LuaJni.dump(ptr) ?: throw LuaException("lua_dump failed")
        } finally {
            LuaJni.setTop(ptr, -2)
        }
    }

    override fun getGlobal(name: String): LuaValue {
        checkOpen()
        LuaJni.getGlobal(ptr, name.encodeToByteArray())
        return try {
            marshal(ptr, -1)
        } finally {
            LuaJni.setTop(ptr, -2)
        }
    }

    override fun setGlobal(name: String, value: LuaValue) {
        checkOpen()
        push(ptr, value)
        LuaJni.setGlobal(ptr, name.encodeToByteArray())
    }

    override fun call(function: LuaValue.Function, args: List<LuaValue>): List<LuaValue> {
        checkOpen()
        val handle = function as? JniFunctionHandle
        require(handle != null && handle.owner === this) {
            "function handle was not created by this LuaState"
        }
        val base = LuaJni.getTop(ptr)
        LuaJni.checkStack(ptr, args.size + 2)
        LuaJni.getRef(ptr, handle.ref)
        for (arg in args) push(ptr, arg)
        return runPcall(base, nargs = args.size)
    }

    override fun register(name: String, function: LuaFunction) {
        checkOpen()
        LuaJni.pushClosure(ptr, handlers.add(function))
        LuaJni.setGlobal(ptr, name.encodeToByteArray())
    }

    override fun wrap(function: LuaFunction): LuaValue.Function {
        checkOpen()
        LuaJni.pushClosure(ptr, handlers.add(function))
        return JniFunctionHandle(this, LuaJni.ref(ptr))
    }

    override fun newTable(): LuaValue.Table {
        checkOpen()
        LuaJni.newTable(ptr)
        return JniTableHandle(this, LuaJni.ref(ptr))
    }

    override fun close() {
        if (closed) return
        checkOwnerThread()
        markClosed()
        handlers.clear()
        LuaJni.close(ptr)
        JniStateRegistry.remove(ptr)
    }

    private fun loadChunk(bytes: ByteArray, chunkName: String) {
        val status = LuaJni.loadBuffer(ptr, bytes, chunkName.encodeToByteArray())
        if (status != LUA_STATUS_OK) throwTopError(status)
    }

    private fun runPcall(base: Int, nargs: Int): List<LuaValue> {
        val status = LuaJni.pcall(ptr, nargs, LUA_MULTIPLE_RETURNS)
        if (status != LUA_STATUS_OK) throwTopError(status)
        return try {
            (base + 1..LuaJni.getTop(ptr)).map { marshal(ptr, it) }
        } finally {
            LuaJni.setTop(ptr, base)
        }
    }

    /** Reads the error message from the stack top, pops it, and throws. */
    internal fun throwTopError(status: Int): Nothing {
        val message = LuaJni.toStringBytes(ptr, -1)?.decodeToString() ?: "unknown Lua error"
        LuaJni.setTop(ptr, -2)
        throw luaErrorFor(status, message)
    }

    /**
     * Converts the value at [idx] to a LuaValue without disturbing the stack.
     * [state] is the calling thread's lua_State (== [ptr] except when dispatch
     * runs inside a Lua coroutine); registry refs are shared between threads.
     */
    internal fun marshal(state: Long, idx: Int): LuaValue = when (val t = LuaJni.type(state, idx)) {
        LUA_TYPE_NIL, LUA_TYPE_NONE -> LuaValue.Nil
        LUA_TYPE_BOOLEAN -> LuaValue.Bool(LuaJni.toBoolean(state, idx))
        LUA_TYPE_NUMBER ->
            if (LuaJni.isInteger(state, idx)) LuaValue.Integer(LuaJni.toInteger(state, idx))
            else LuaValue.Number(LuaJni.toNumber(state, idx))
        LUA_TYPE_STRING -> LuaValue.Str(LuaJni.toStringBytes(state, idx)?.decodeToString() ?: "")
        LUA_TYPE_TABLE -> {
            LuaJni.pushValue(state, idx)
            JniTableHandle(this, LuaJni.ref(state))
        }
        LUA_TYPE_FUNCTION -> {
            LuaJni.pushValue(state, idx)
            JniFunctionHandle(this, LuaJni.ref(state))
        }
        LUA_TYPE_LIGHTUSERDATA, LUA_TYPE_USERDATA -> LuaValue.Userdata(luaTypeName(t))
        else -> throw LuaException("unsupported Lua value of type ${luaTypeName(t)}")
    }

    internal fun push(state: Long, value: LuaValue) {
        when (value) {
            LuaValue.Nil -> LuaJni.pushNil(state)
            is LuaValue.Bool -> LuaJni.pushBoolean(state, value.value)
            is LuaValue.Integer -> LuaJni.pushInteger(state, value.value)
            is LuaValue.Number -> LuaJni.pushNumber(state, value.value)
            is LuaValue.Str -> LuaJni.pushString(state, value.value.encodeToByteArray())
            is LuaValue.Userdata -> LuaJni.pushNil(state)
            is JniTableHandle -> {
                require(value.owner === this) { "table handle was not created by this LuaState" }
                LuaJni.getRef(state, value.ref)
            }
            is JniFunctionHandle -> {
                require(value.owner === this) { "function handle was not created by this LuaState" }
                LuaJni.getRef(state, value.ref)
            }
            is LuaValue.Table, is LuaValue.Function ->
                throw IllegalArgumentException("value was not created by this LuaState")
        }
    }
}

internal class JniTableHandle(
    internal val owner: JniLuaState,
    internal val ref: Int,
) : LuaValue.Table {

    override fun get(key: LuaValue): LuaValue {
        owner.checkOpen()
        val p = owner.ptr
        LuaJni.checkStack(p, 3)
        LuaJni.getRef(p, ref)
        owner.push(p, key)
        val status = LuaJni.pGetTable(p)
        if (status != LUA_STATUS_OK) owner.throwTopError(status)
        return try {
            owner.marshal(p, -1)
        } finally {
            LuaJni.setTop(p, -2)
        }
    }

    override fun set(key: LuaValue, value: LuaValue) {
        owner.checkOpen()
        val p = owner.ptr
        LuaJni.checkStack(p, 4)
        LuaJni.getRef(p, ref)
        owner.push(p, key)
        owner.push(p, value)
        val status = LuaJni.pSetTable(p)
        if (status != LUA_STATUS_OK) owner.throwTopError(status)
    }

    override fun toMap(): Map<LuaValue, LuaValue> {
        owner.checkOpen()
        val p = owner.ptr
        val result = LinkedHashMap<LuaValue, LuaValue>()
        LuaJni.checkStack(p, 4)
        LuaJni.getRef(p, ref)
        val t = LuaJni.getTop(p)
        try {
            LuaJni.pushNil(p)
            while (LuaJni.next(p, t) != 0) {
                result[owner.marshal(p, t + 1)] = owner.marshal(p, t + 2)
                LuaJni.setTop(p, -2)
            }
        } finally {
            LuaJni.setTop(p, t - 1)
        }
        return result
    }

    override val size: Int
        get() {
            owner.checkOpen()
            val p = owner.ptr
            LuaJni.checkStack(p, 2)
            LuaJni.getRef(p, ref)
            val status = LuaJni.pLen(p)
            if (status != LUA_STATUS_OK) owner.throwTopError(status)
            val length = LuaJni.toInteger(p, -1)
            LuaJni.setTop(p, -2)
            return length.toInt()
        }
}

internal class JniFunctionHandle(
    internal val owner: JniLuaState,
    internal val ref: Int,
) : LuaValue.Function
