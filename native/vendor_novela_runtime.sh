#!/usr/bin/env bash
#
# Vendors native libraries for the NoveLA scraping runtime:
# - Lexbor HTML5 parser & CSS selector engine
# - yyjson fast JSON parser
# - tiny-aes-c AES implementation
#
# Usage: ./vendor_novela_runtime.sh

set -euo pipefail

cd "$(dirname "$0")"

LEXBOR_VERSION="3.0.0"

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

echo "=== 1. Vendoring Lexbor $LEXBOR_VERSION ==="
curl -fsSL -o "$workdir/lexbor.tar.gz" "https://github.com/lexbor/lexbor/archive/refs/tags/v$LEXBOR_VERSION.tar.gz"
tar -xzf "$workdir/lexbor.tar.gz" -C "$workdir"
rm -rf lexbor/source lexbor/LICENSE
mkdir -p lexbor
cp -r "$workdir/lexbor-$LEXBOR_VERSION/source" lexbor/
cp "$workdir/lexbor-$LEXBOR_VERSION/LICENSE" lexbor/LICENSE
echo "$LEXBOR_VERSION" > lexbor/VERSION
echo "Lexbor $LEXBOR_VERSION vendored into $(pwd)/lexbor"

echo "=== 2. Vendoring yyjson ==="
mkdir -p yyjson
curl -fsSL -o yyjson/yyjson.h https://raw.githubusercontent.com/ibireme/yyjson/master/src/yyjson.h
curl -fsSL -o yyjson/yyjson.c https://raw.githubusercontent.com/ibireme/yyjson/master/src/yyjson.c
echo "yyjson vendored into $(pwd)/yyjson"

echo "=== 3. Vendoring tiny-aes-c ==="
mkdir -p crypto/aes
curl -fsSL -o crypto/aes/aes.h https://raw.githubusercontent.com/kokke/tiny-AES-c/master/aes.h
curl -fsSL -o crypto/aes/aes.c https://raw.githubusercontent.com/kokke/tiny-AES-c/master/aes.c
echo "tiny-aes-c vendored into $(pwd)/crypto/aes"

echo "=== 4. Vendoring PCRE2 10.49 ==="
PCRE2_VERSION="10.49"
curl -fsSL -o "$workdir/pcre2.tar.gz" "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-$PCRE2_VERSION/pcre2-$PCRE2_VERSION.tar.gz"
tar -xzf "$workdir/pcre2.tar.gz" -C "$workdir"
rm -rf pcre2/src pcre2/LICENCE.md
mkdir -p pcre2/src
cp "$workdir/pcre2-$PCRE2_VERSION/src/"*.c "$workdir/pcre2-$PCRE2_VERSION/src/"*.h pcre2/src/
cp "$workdir/pcre2-$PCRE2_VERSION/src/config.h.generic" pcre2/src/config.h
cp "$workdir/pcre2-$PCRE2_VERSION/src/pcre2.h.generic" pcre2/src/pcre2.h
cp "$workdir/pcre2-$PCRE2_VERSION/src/pcre2_chartables.c.dist" pcre2/src/pcre2_chartables.c
cp "$workdir/pcre2-$PCRE2_VERSION/LICENCE.md" pcre2/LICENCE.md
echo "$PCRE2_VERSION" > pcre2/VERSION
echo "PCRE2 $PCRE2_VERSION vendored into $(pwd)/pcre2"

echo "=== Native runtime libraries successfully vendored ==="

