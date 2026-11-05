#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$DIR"

LEXBOR_SRCS=$(find native/lexbor/source/lexbor -name "*.c" ! -path "*/ports/windows_nt/*")
PCRE2_SRCS=$(find native/pcre2/src -maxdepth 1 -name "pcre2_*.c" \
    ! -name "pcre2_fuzzsupport.c" ! -name "pcre2_dftables.c" \
    ! -name "pcre2_jit_test.c")

OUT_BIN="$DIR/native/tests/test_runner_bin"
CACHE_DIR="$DIR/native/tests/.build_cache"
mkdir -p "$CACHE_DIR"

CFLAGS="-O2 -I native/lua/src -I native/shim -I native/modules -I native/lexbor/source -I native/pcre2/src -I native/yyjson -I native/crypto/aes -I native/crypto/base64 -I native/crypto/hash -DLUA_USE_LINUX -DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=8"

echo "==> Compiling source files..."
ALL_OBJS=""
for src in $LEXBOR_SRCS $PCRE2_SRCS native/shim/lua_all.c native/yyjson/yyjson.c native/crypto/aes/aes.c native/crypto/base64/base64.c native/crypto/hash/md5.c native/crypto/hash/sha256.c; do
    obj="$CACHE_DIR/$(echo "$src" | tr '/.' '__').o"
    ALL_OBJS="$ALL_OBJS $obj"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        clang $CFLAGS -c "$src" -o "$obj"
    fi
done

# Always recompile modules under active test
clang $CFLAGS \
    native/tests/test_runner.c \
    native/shim/lua_shim.c \
    native/modules/lua_html.c \
    native/modules/lua_pcre.c \
    native/modules/lua_json.c \
    native/modules/lua_crypto.c \
    native/modules/lua_utils.c \
    $ALL_OBJS \
    -lm -o "$OUT_BIN"

echo "==> Executing test suite..."
"$OUT_BIN" native/tests/test_novela_runtime.lua
