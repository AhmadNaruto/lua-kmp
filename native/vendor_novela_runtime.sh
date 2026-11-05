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

echo "=== Native runtime libraries successfully vendored ==="
