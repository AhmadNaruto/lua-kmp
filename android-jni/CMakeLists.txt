cmake_minimum_required(VERSION 3.22)
project(luakmp C)

set(NATIVE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../native)

# 1. Lexbor HTML5 & CSS Selector Engine
file(GLOB_RECURSE LEXBOR_SOURCES ${NATIVE_DIR}/lexbor/source/lexbor/*.c)
list(FILTER LEXBOR_SOURCES EXCLUDE REGEX ".*/ports/windows_nt/.*")

# 2. PCRE2 8-bit Regex Engine
file(GLOB PCRE2_SOURCES ${NATIVE_DIR}/pcre2/src/*.c)
list(FILTER PCRE2_SOURCES EXCLUDE REGEX ".*/pcre2_(fuzzsupport|dftables|jit_test|posix_test|test).c")
list(FILTER PCRE2_SOURCES EXCLUDE REGEX ".*/pcre2(demo|grep|posix|test|posix_test).c")

# 3. Native Modules & Crypto Utilities
set(RUNTIME_SOURCES
    ${NATIVE_DIR}/yyjson/yyjson.c
    ${NATIVE_DIR}/crypto/aes/aes.c
    ${NATIVE_DIR}/crypto/base64/base64.c
    ${NATIVE_DIR}/crypto/hash/md5.c
    ${NATIVE_DIR}/crypto/hash/sha256.c
    ${NATIVE_DIR}/modules/lua_html.c
    ${NATIVE_DIR}/modules/lua_pcre.c
    ${NATIVE_DIR}/modules/lua_json.c
    ${NATIVE_DIR}/modules/lua_crypto.c
)

add_library(luakmp SHARED
    ${NATIVE_DIR}/shim/lua_all.c
    ${NATIVE_DIR}/shim/lua_shim.c
    ${NATIVE_DIR}/jni/lua_jni.c
    ${LEXBOR_SOURCES}
    ${PCRE2_SOURCES}
    ${RUNTIME_SOURCES}
)

target_include_directories(luakmp PRIVATE
    ${NATIVE_DIR}/lua/src
    ${NATIVE_DIR}/shim
    ${NATIVE_DIR}/modules
    ${NATIVE_DIR}/lexbor/source
    ${NATIVE_DIR}/pcre2/src
    ${NATIVE_DIR}/yyjson
    ${NATIVE_DIR}/crypto/aes
    ${NATIVE_DIR}/crypto/base64
    ${NATIVE_DIR}/crypto/hash
)

# Bionic is close enough to a regular Linux for luaconf.h's purposes.
target_compile_definitions(luakmp PRIVATE
    LUA_USE_LINUX
    HAVE_CONFIG_H
    PCRE2_CODE_UNIT_WIDTH=8
)

target_compile_options(luakmp PRIVATE -O2 -fvisibility=hidden)

target_link_libraries(luakmp m dl)
