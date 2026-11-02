# lua-kmp

Kotlin Multiplatform bindings for the **reference Lua 5.5 interpreter** — one
interpreter, one language version, on every target. The vendored Lua C sources
are compiled into each artifact (via cinterop on Kotlin/Native, via JNI on the
JVM and Android), so scripts behave identically everywhere, including iOS.

```kotlin
LuaState().use { lua ->
    lua.eval("return 1 + 1")   // [LuaValue.Integer(2)]
}
```

## Supported platforms

| Target | Backend | Native code |
| --- | --- | --- |
| `jvm` (macOS arm64/x64, Linux x64/arm64, Windows x64) | JNI | bundled in the jar, extracted at runtime |
| `androidTarget` (arm64-v8a, armeabi-v7a, x86_64; minSdk 24) | JNI | built by the NDK, packaged in the AAR |
| `iosArm64`, `iosSimulatorArm64` | cinterop | compiled into the klib |
| `macosArm64`, `macosX64` | cinterop | compiled into the klib |
| `linuxX64` | cinterop | compiled into the klib |
| `mingwX64` | cinterop | compiled into the klib |

Not planned for v1: LuaJIT (cannot follow 5.5 semantics), JS/wasm targets, and
automatic reflective binding of Kotlin objects — host functions are explicit.

## Installation

```kotlin
kotlin {
    sourceSets {
        commonMain.dependencies {
            implementation("com.seanproctor:lua-kmp:<version>")
        }
    }
}
```

## Usage

### Evaluating scripts

```kotlin
import com.seanproctor.lua.*

LuaState().use { lua ->
    // eval returns everything the chunk returns, marshalled to LuaValue.
    val results = lua.eval("return 1 + 1, 'hello', 3.0")
    // [Integer(2), Str("hello"), Number(3.0)] — the 5.5 integer/float
    // distinction is preserved: 3 is Integer, 3.0 is Number.

    lua.setGlobal("greeting", LuaValue.Str("hi"))
    lua.eval("assert(greeting == 'hi')")

    // load compiles without running; call it later, with arguments.
    val chunk = lua.load("local a, b = ... ; return a + b")
    lua.call(chunk, listOf(LuaValue.Integer(2), LuaValue.Integer(3)))  // [Integer(5)]
}
```

### Tables

Tables cross the boundary as opaque handles backed by the Lua registry, so
reads and writes are always live — no copying unless you ask for it.

```kotlin
val t = lua.eval("return {greeting = 'hello'}").single() as LuaValue.Table
t[LuaValue.Str("greeting")]                      // Str("hello")
t[LuaValue.Str("count")] = LuaValue.Integer(1)   // visible to Lua immediately
t.size                                           // the # operator
t.toMap()                                        // shallow copy as a Kotlin Map
```

### Host functions

Register Kotlin functions and call them from Lua. Arguments arrive already
marshalled; returned values are pushed back. Handlers may throw — the
exception becomes a regular Lua error (catchable with `pcall`), and if the
script does not catch it, it surfaces to the caller as `LuaRuntimeError`.

```kotlin
lua.register("greet") { args ->
    val name = (args.first() as LuaValue.Str).value
    listOf(LuaValue.Str("hello, $name"))
}
lua.eval("print(greet('world'))")

// wrap creates a function value without naming it, e.g. to put in a table:
val api = lua.eval("api = {} ; return api").single() as LuaValue.Table
api[LuaValue.Str("double")] = lua.wrap { args ->
    listOf(LuaValue.Integer((args.single() as LuaValue.Integer).value * 2))
}
```

Host functions work inside Lua coroutines too.

## Sandboxing

By default only a safe subset of the standard library is opened. Selection
uses Lua 5.5's `luaL_openselectedlibs` — unselected libraries are never
loaded, not loaded-then-hidden.

```kotlin
// Default: BASE, COROUTINE, TABLE, STRING, MATH, UTF8 (no io/os/package/debug).
LuaState()

// Everything, including io and os:
LuaState(LuaConfig(stdlibs = StdLib.ALL))

// Or any explicit subset:
LuaState(LuaConfig(stdlibs = setOf(StdLib.BASE, StdLib.MATH)))
```

Caveat: the base library itself includes `dofile` and `loadfile`, which read
from the filesystem. If your sandbox must forbid all file access, clear them
after construction:

```kotlin
lua.eval("dofile = nil ; loadfile = nil")
```

## Errors

All failures surface as one hierarchy:

| Exception | Meaning |
| --- | --- |
| `LuaSyntaxError` | chunk failed to compile |
| `LuaRuntimeError` | script/function raised at run time (incl. uncaught host-function exceptions) |
| `LuaMemoryError` | allocation failure in the interpreter |
| `LuaException` | base class (also: unsupported value marshalling) |

Using a closed state, or touching it from the wrong thread, throws
`IllegalStateException`. Passing another state's handle throws
`IllegalArgumentException`.

## Threading and lifetime

- A `LuaState` is **confined to the thread that created it**; there is no
  internal locking. Cross-thread use throws immediately.
- `close()` frees the interpreter and every pinned host-function reference.
  Idempotent. All `Table`/`Function` handles die with their state.
- Handles are registry-backed: they stay valid across stack operations but
  pin their Lua values until `close()`. Holding very many handles delays
  collection of those values; an explicit per-handle `release()` is planned
  post-v1.

## Lua 5.5 notes for script authors

lua-kmp embeds Lua 5.5 (currently 5.5.1, vendored and pinned by checksum).
If your scripts come from 5.4, the main script-facing changes are:

- **Declarations for global variables** — chunks can declare globals
  explicitly (`global x`); see §3.3.9 of the manual.
- **For-loop control variables are read-only** — assigning to them is now a
  compile-time error.
- **Float printing changed** — floats print in decimal with enough digits to
  read back exactly (`0.1` no longer prints as `0.1` rounded to 14 digits).
- Also new: deeper table constructors, `table.create`, extended
  `utf8.offset`, and incremental major GC. Binary chunks are refused by this
  binding (`load` accepts text only).

## Sample

A runnable feature tour lives in [`sample/`](sample/): the same `commonMain`
demo runs on the JVM (`./gradlew :sample:runJvm`) and as a native executable
(`./gradlew :sample:runDebugExecutableLinuxX64`, `...MacosArm64`, …),
printing identical output on every backend.

## Building from source

Requirements: JDK 17+, a C toolchain (`cc`/gcc/clang; MinGW gcc on Windows),
and for the Android target an SDK with NDK (set `ANDROID_HOME` or
`local.properties`; without one, the Android target is skipped).

```sh
./gradlew build                # everything buildable on this host, plus tests
./gradlew linuxX64Test jvmTest # the local test matrix on Linux
./gradlew :lua-kmp:assemble :android-jni:assembleRelease   # the Android AARs
```

The build is three modules: `:lua-kmp` is the library, `:sample` the feature
tour, and `:android-jni` an AAR that contains nothing but `libluakmp.so` for
each ABI. That split is not cosmetic - the Android KMP Gradle plugin cannot run
a CMake build, so the CMake build lives in `:android-jni` under the classic
`com.android.library` plugin and `androidMain` depends on it at runtime. Android
consumers therefore resolve two artifacts, `lua-kmp-android` and
`lua-kmp-android-jni`, and the two are always released together.

- Apple targets build and test on macOS hosts only.
- `native/revendor.sh` re-downloads and verifies the pinned Lua release into
  `native/lua/` (update `LUA_VERSION`/`LUA_SHA256` there to move versions).
- The vendored sources compile once per target: cinterop packs them into each
  Kotlin/Native klib (see `lua-kmp/src/nativeInterop/cinterop/lua.def`), CMake builds
  the Android `.so`, and a per-host Gradle task builds the JVM JNI library
  into `lua-kmp/build/jniStaging/`, which is bundled into the jvm jar under
  `native/<os>-<arch>/`.

## Releasing

Tag `v*` and CI publishes every target to Maven Central (portal API) from a
macOS runner, merging the JVM native libraries staged by the Linux, macOS,
and Windows jobs. Required repository secrets: `MAVEN_CENTRAL_USERNAME`,
`MAVEN_CENTRAL_PASSWORD`, `SIGNING_IN_MEMORY_KEY` (base64 of the binary
secret keyring, e.g. `base64 -w0 < secring.gpg`), `SIGNING_IN_MEMORY_KEY_ID`
(the short 8-char key id), and `SIGNING_IN_MEMORY_KEY_PASSWORD`.

## License

MIT. Bundles the Lua 5.5 sources, © 1994–2026 Lua.org, PUC-Rio, also MIT;
the Lua copyright notice ships in all distributions (`native/lua/LICENSE`).
