#!/usr/bin/env bash
#
# Vendors a pinned Lua release into native/lua/src.
#
# Downloads the official tarball from lua.org, verifies its checksum, and
# unpacks the src/ directory only. The standalone interpreter and compiler
# entry points (lua.c, luac.c) are dropped because they are not part of the
# library build. A VERSION file records the vendored release, and LICENSE is
# regenerated from the copyright notice in lua.h.
#
# Usage: ./revendor.sh
# To move to a new release, update LUA_VERSION and LUA_SHA256 below.

set -euo pipefail

LUA_VERSION="5.5.1"
LUA_SHA256="1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce"

cd "$(dirname "$0")"

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

tarball="$workdir/lua-$LUA_VERSION.tar.gz"
echo "Downloading lua-$LUA_VERSION.tar.gz ..."
curl -fsSL -o "$tarball" "https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz"
echo "$LUA_SHA256  $tarball" | sha256sum -c -

tar -xzf "$tarball" -C "$workdir"

rm -rf lua/src
mkdir -p lua/src
cp "$workdir/lua-$LUA_VERSION/src/"*.c "$workdir/lua-$LUA_VERSION/src/"*.h lua/src/
rm -f lua/src/lua.c lua/src/luac.c

echo "$LUA_VERSION" > lua/VERSION

# Extract the MIT license text from lua.h so the bundled notice always matches
# the vendored release.
sed -n '/^\* Copyright (C)/,/DEALINGS IN THE SOFTWARE/p' lua/src/lua.h \
  | sed 's/^\* \{0,1\}//' > lua/LICENSE

echo "Vendored Lua $LUA_VERSION into $(pwd)/lua/src"
