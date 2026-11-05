package com.seanproctor.lua

/**
 * A single Lua 5.5 interpreter instance.
 *
 * A state is confined to the thread that created it; touching it from any
 * other thread throws [IllegalStateException]. [close] frees the underlying
 * `lua_State` and disposes every pinned host-function reference; using the
 * state (or any [LuaValue.Table]/[LuaValue.Function] handle it produced)
 * after `close()` throws [IllegalStateException].
 *
 * [LuaValue.Table] and [LuaValue.Function] handles are backed by references
 * in the Lua registry, so they remain valid across stack operations but stay
 * pinned until the state is closed. Holding very many handles therefore keeps
 * their Lua values alive until `close()`.
 */
public interface LuaState : AutoCloseable {

    /** Loads and runs a chunk. Returns the values the chunk returns. Throws [LuaException] on error. */
    public fun eval(code: String, chunkName: String = "=(load)"): List<LuaValue>

    /** Loads a chunk without running it, returning it as a callable [LuaValue.Function]. */
    public fun load(code: String, chunkName: String = "=(load)"): LuaValue.Function

    /** Loads a compiled bytecode or text chunk without running it, returning it as a callable [LuaValue.Function]. */
    public fun load(bytes: ByteArray, chunkName: String = "=(load)"): LuaValue.Function

    /** Dumps the function as precompiled binary bytecode. */
    public fun dump(function: LuaValue.Function): ByteArray

    /** Reads a global variable. Absent globals come back as [LuaValue.Nil]. */
    public fun getGlobal(name: String): LuaValue

    /** Writes a global variable. */
    public fun setGlobal(name: String, value: LuaValue)

    /** Calls a Lua function value with [args], returning its results. Uses `lua_pcall`; maps errors. */
    public fun call(function: LuaValue.Function, args: List<LuaValue> = emptyList()): List<LuaValue>

    /** Registers a Kotlin function as a global callable from Lua. */
    public fun register(name: String, function: LuaFunction)

    /** Wraps a Kotlin function as a [LuaValue.Function] (for putting into tables, returning, etc.). */
    public fun wrap(function: LuaFunction): LuaValue.Function

    /** Creates a new empty Lua table. */
    public fun newTable(): LuaValue.Table
}

/** Factory. */
public fun LuaState(config: LuaConfig = LuaConfig()): LuaState = JniLuaState(config)
