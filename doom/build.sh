#!/bin/sh
# Build everything for the ATOTO F7 (Sunplus, ARMv7 soft-float) and assemble the
# USB-stick bundle in dist/doom/. Needs zig (cross C compiler) and make.
# doom1.wad (shareware) is not in git: put it at doom/wad/doom1.wad first.
set -e
cd "$(dirname "$0")"
CC="zig cc -target arm-linux-musleabi"

( cd src/fbdoom && make NOSDL=1 CROSS_COMPILE="" CC="$CC" LDFLAGS="-static -Wl,--gc-sections" )
$CC -static -O2 -s -Wall -o launcher/f7doomd launcher/f7doomd.c
$CC -static -O2 -s -o keepalive/mcu_keepalive keepalive/mcu_keepalive.c

rm -rf dist; mkdir -p dist/doom
cp src/fbdoom/fbdoom launcher/f7doomd keepalive/mcu_keepalive \
   launcher/doom_run.sh launcher/start.sh dist/doom/
if [ -f wad/doom1.wad ]; then cp wad/doom1.wad dist/doom/; else echo "NOTE: add wad/doom1.wad to the bundle"; fi
echo "bundle: $(pwd)/dist/doom"; ls -la dist/doom
