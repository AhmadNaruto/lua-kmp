package com.seanproctor.lua

/**
 * A Lua value marshalled across the Kotlin/Lua boundary.
 *
 * Primitives ([Nil], [Bool], [Integer], [Number], [Str]) are plain data;
 * [Table] and [Function] are opaque handles backed by references in the Lua
 * registry of the state that produced them. Handles can only be passed back
 * to their owning [LuaState] and are released when that state is closed.
 *
 * Lua 5.5 numbers have two subtypes, and the distinction is preserved:
 * `3` marshals as [Integer], `3.0` as [Number].
 */
public sealed interface LuaValue {

    public data object Nil : LuaValue

    public data class Bool(public val value: Boolean) : LuaValue

    /** A Lua integer (the Lua 5.5 integer subtype, 64-bit). */
    public data class Integer(public val value: Long) : LuaValue

    /** A Lua float (the Lua 5.5 float subtype, double). */
    public data class Number(public val value: Double) : LuaValue

    public data class Str(public val value: String) : LuaValue

    /** Opaque handle to a Lua table living in the registry; provides map/list accessors. */
    public interface Table : LuaValue {
        public operator fun get(key: LuaValue): LuaValue
        public operator fun set(key: LuaValue, value: LuaValue)
        public fun toMap(): Map<LuaValue, LuaValue>

        /** Border length (the `#` operator, honoring any `__len` metamethod). */
        public val size: Int
    }

    /** Opaque handle to a Lua function living in the registry. */
    public interface Function : LuaValue
}

/** SAM for host functions. Receives already-marshalled args; returns values to push back. */
public fun interface LuaFunction {
    public fun invoke(args: List<LuaValue>): List<LuaValue>
}
