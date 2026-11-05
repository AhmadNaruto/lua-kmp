package com.seanproctor.lua

import com.seanproctor.lua.cinterop.LUA_MULTRET
import com.seanproctor.lua.cinterop.LUA_OK
import com.seanproctor.lua.cinterop.LUA_TBOOLEAN
import com.seanproctor.lua.cinterop.LUA_TFUNCTION
import com.seanproctor.lua.cinterop.LUA_TNIL
import com.seanproctor.lua.cinterop.LUA_TNONE
import com.seanproctor.lua.cinterop.LUA_TNUMBER
import com.seanproctor.lua.cinterop.LUA_TSTRING
import com.seanproctor.lua.cinterop.LUA_TTABLE
import com.seanproctor.lua.cinterop.kl_dump
import com.seanproctor.lua.cinterop.kl_getref
import com.seanproctor.lua.cinterop.kl_loadbuffer
import com.seanproctor.lua.cinterop.kl_openlibs
import com.seanproctor.lua.cinterop.kl_pcall
import com.seanproctor.lua.cinterop.kl_pgettable
import com.seanproctor.lua.cinterop.kl_plen
import com.seanproctor.lua.cinterop.kl_pop
import com.seanproctor.lua.cinterop.kl_psettable
import com.seanproctor.lua.cinterop.kl_push_dispatch_closure
import com.seanproctor.lua.cinterop.kl_pushlstring
import com.seanproctor.lua.cinterop.kl_ref
import com.seanproctor.lua.cinterop.kl_setextra
import com.seanproctor.lua.cinterop.kl_tolstring
import com.seanproctor.lua.cinterop.lua_State
import com.seanproctor.lua.cinterop.lua_checkstack
import com.seanproctor.lua.cinterop.lua_close
import com.seanproctor.lua.cinterop.lua_getglobal
import com.seanproctor.lua.cinterop.lua_gettop
import com.seanproctor.lua.cinterop.lua_isinteger
import com.seanproctor.lua.cinterop.lua_next
import com.seanproctor.lua.cinterop.lua_pushboolean
import com.seanproctor.lua.cinterop.lua_pushinteger
import com.seanproctor.lua.cinterop.lua_pushnil
import com.seanproctor.lua.cinterop.lua_pushnumber
import com.seanproctor.lua.cinterop.lua_pushvalue
import com.seanproctor.lua.cinterop.lua_setglobal
import com.seanproctor.lua.cinterop.lua_settop
import com.seanproctor.lua.cinterop.lua_toboolean
import com.seanproctor.lua.cinterop.lua_tointegerx
import com.seanproctor.lua.cinterop.lua_tonumberx
import com.seanproctor.lua.cinterop.lua_type
import com.seanproctor.lua.cinterop.luaL_newstate
import com.seanproctor.lua.internal.BaseLuaState
import com.seanproctor.lua.internal.LeakTracker
import com.seanproctor.lua.internal.ensureHostCallbackInstalled
import com.seanproctor.lua.internal.luaErrorFor
import com.seanproctor.lua.internal.luaTypeName
import com.seanproctor.lua.internal.toMask
import kotlinx.cinterop.CPointer
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.IntVar
import kotlinx.cinterop.StableRef
import kotlinx.cinterop.alloc
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.ptr
import kotlinx.cinterop.readBytes
import kotlinx.cinterop.value
import kotlinx.cinterop.usePinned
import kotlinx.cinterop.addressOf

internal actual fun createLuaState(config: LuaConfig): LuaState = NativeLuaState(config)

@OptIn(ExperimentalForeignApi::class)
internal class NativeLuaState(config: LuaConfig) : BaseLuaState() {

    internal val L: CPointer<lua_State>
    private val self: StableRef<NativeLuaState>

    init {
        ensureHostCallbackInstalled()
        L = luaL_newstate() ?: throw LuaMemoryError("cannot allocate a new lua_State")
        self = StableRef.create(this)
        LeakTracker.statePinned()
        kl_setextra(L, self.asCPointer())
        kl_openlibs(L, config.stdlibs.toMask())
    }

    override fun eval(code: String, chunkName: String): List<LuaValue> {
        checkOpen()
        val base = lua_gettop(L)
        loadChunk(code.encodeToByteArray(), chunkName)
        return runPcall(base, nargs = 0)
    }

    override fun load(code: String, chunkName: String): LuaValue.Function =
        load(code.encodeToByteArray(), chunkName)

    override fun load(bytes: ByteArray, chunkName: String): LuaValue.Function {
        checkOpen()
        loadChunk(bytes, chunkName)
        return NativeFunctionHandle(this, kl_ref(L))
    }

    override fun dump(function: LuaValue.Function): ByteArray {
        checkOpen()
        val handle = function as? NativeFunctionHandle
        require(handle != null && handle.owner === this) {
            "function handle was not created by this LuaState"
        }
        lua_checkstack(L, 2)
        kl_getref(L, handle.ref)
        try {
            return memScoped {
                val lenVar = alloc<IntVar>()
                val ptr = kl_dump(L, lenVar.ptr) ?: throw LuaException("lua_dump failed")
                val len = lenVar.value
                val bytes = ptr.readBytes(len)
                platform.posix.free(ptr)
                bytes
            }
        } finally {
            lua_settop(L, -2)
        }
    }

    override fun getGlobal(name: String): LuaValue {
        checkOpen()
        lua_getglobal(L, name)
        return try {
            marshal(L, -1)
        } finally {
            kl_pop(L, 1)
        }
    }

    override fun setGlobal(name: String, value: LuaValue) {
        checkOpen()
        push(L, value)
        lua_setglobal(L, name)
    }

    override fun call(function: LuaValue.Function, args: List<LuaValue>): List<LuaValue> {
        checkOpen()
        val handle = function as? NativeFunctionHandle
        require(handle != null && handle.owner === this) {
            "function handle was not created by this LuaState"
        }
        val base = lua_gettop(L)
        lua_checkstack(L, args.size + 2)
        kl_getref(L, handle.ref)
        for (arg in args) push(L, arg)
        return runPcall(base, nargs = args.size)
    }

    override fun register(name: String, function: LuaFunction) {
        checkOpen()
        kl_push_dispatch_closure(L, handlers.add(function))
        lua_setglobal(L, name)
    }

    override fun wrap(function: LuaFunction): LuaValue.Function {
        checkOpen()
        kl_push_dispatch_closure(L, handlers.add(function))
        return NativeFunctionHandle(this, kl_ref(L))
    }

    override fun close() {
        if (closed) return
        checkOwnerThread()
        markClosed()
        handlers.clear()
        lua_close(L)
        self.dispose()
        LeakTracker.stateReleased()
    }

    private fun loadChunk(bytes: ByteArray, chunkName: String) {
        val status = if (bytes.isEmpty()) {
            kl_loadbuffer(L, "", 0, chunkName)
        } else {
            bytes.usePinned { pinned ->
                kl_loadbuffer(L, pinned.addressOf(0), bytes.size, chunkName)
            }
        }
        if (status != LUA_OK) throwTopError(status)
    }

    private fun runPcall(base: Int, nargs: Int): List<LuaValue> {
        val status = kl_pcall(L, nargs, LUA_MULTRET)
        if (status != LUA_OK) throwTopError(status)
        return try {
            (base + 1..lua_gettop(L)).map { marshal(L, it) }
        } finally {
            lua_settop(L, base)
        }
    }

    /** Reads the error message from the stack top, pops it, and throws. */
    internal fun throwTopError(status: Int): Nothing {
        val message = stringAt(L, -1) ?: "unknown Lua error"
        kl_pop(L, 1)
        throw luaErrorFor(status, message)
    }

    /**
     * Converts the value at [idx] to a LuaValue without disturbing the stack.
     * Tables and functions are ref'd into the registry and returned as handles.
     * Works on any thread of this state (dispatch may run inside a coroutine).
     */
    internal fun marshal(L: CPointer<lua_State>, idx: Int): LuaValue = when (val t = lua_type(L, idx)) {
        LUA_TNIL, LUA_TNONE -> LuaValue.Nil
        LUA_TBOOLEAN -> LuaValue.Bool(lua_toboolean(L, idx) != 0)
        LUA_TNUMBER ->
            if (lua_isinteger(L, idx) != 0) LuaValue.Integer(lua_tointegerx(L, idx, null))
            else LuaValue.Number(lua_tonumberx(L, idx, null))
        LUA_TSTRING -> LuaValue.Str(stringAt(L, idx) ?: "")
        LUA_TTABLE -> {
            lua_pushvalue(L, idx)
            NativeTableHandle(this, kl_ref(L))
        }
        LUA_TFUNCTION -> {
            lua_pushvalue(L, idx)
            NativeFunctionHandle(this, kl_ref(L))
        }
        else -> throw LuaException("unsupported Lua value of type ${luaTypeName(t)}")
    }

    internal fun push(L: CPointer<lua_State>, value: LuaValue) {
        when (value) {
            LuaValue.Nil -> lua_pushnil(L)
            is LuaValue.Bool -> lua_pushboolean(L, if (value.value) 1 else 0)
            is LuaValue.Integer -> lua_pushinteger(L, value.value)
            is LuaValue.Number -> lua_pushnumber(L, value.value)
            is LuaValue.Str ->
                kl_pushlstring(L, value.value, value.value.encodeToByteArray().size)
            is NativeTableHandle -> {
                require(value.owner === this) { "table handle was not created by this LuaState" }
                kl_getref(L, value.ref)
            }
            is NativeFunctionHandle -> {
                require(value.owner === this) { "function handle was not created by this LuaState" }
                kl_getref(L, value.ref)
            }
            is LuaValue.Table, is LuaValue.Function ->
                throw IllegalArgumentException("value was not created by this LuaState")
        }
    }

    /** Copies the string at [idx] out of Lua memory; null if it is not a string. */
    private fun stringAt(L: CPointer<lua_State>, idx: Int): String? = memScoped {
        val len = alloc<IntVar>()
        val ptr = kl_tolstring(L, idx, len.ptr) ?: return null
        if (len.value == 0) "" else ptr.readBytes(len.value).decodeToString()
    }
}

@OptIn(ExperimentalForeignApi::class)
internal class NativeTableHandle(
    internal val owner: NativeLuaState,
    internal val ref: Int,
) : LuaValue.Table {

    override fun get(key: LuaValue): LuaValue {
        owner.checkOpen()
        val L = owner.L
        lua_checkstack(L, 3)
        kl_getref(L, ref)
        owner.push(L, key)
        val status = kl_pgettable(L)
        if (status != LUA_OK) owner.throwTopError(status)
        return try {
            owner.marshal(L, -1)
        } finally {
            kl_pop(L, 1)
        }
    }

    override fun set(key: LuaValue, value: LuaValue) {
        owner.checkOpen()
        val L = owner.L
        lua_checkstack(L, 4)
        kl_getref(L, ref)
        owner.push(L, key)
        owner.push(L, value)
        val status = kl_psettable(L)
        if (status != LUA_OK) owner.throwTopError(status)
    }

    override fun toMap(): Map<LuaValue, LuaValue> {
        owner.checkOpen()
        val L = owner.L
        val result = LinkedHashMap<LuaValue, LuaValue>()
        lua_checkstack(L, 4)
        kl_getref(L, ref)
        val t = lua_gettop(L)
        try {
            lua_pushnil(L)
            while (lua_next(L, t) != 0) {
                result[owner.marshal(L, t + 1)] = owner.marshal(L, t + 2)
                kl_pop(L, 1)
            }
        } finally {
            lua_settop(L, t - 1)
        }
        return result
    }

    override val size: Int
        get() {
            owner.checkOpen()
            val L = owner.L
            lua_checkstack(L, 2)
            kl_getref(L, ref)
            val status = kl_plen(L)
            if (status != LUA_OK) owner.throwTopError(status)
            val length = lua_tointegerx(L, -1, null)
            kl_pop(L, 1)
            return length.toInt()
        }
}

@OptIn(ExperimentalForeignApi::class)
internal class NativeFunctionHandle(
    internal val owner: NativeLuaState,
    internal val ref: Int,
) : LuaValue.Function
