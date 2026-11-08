#!/bin/sh
# Launched by f7doomd on F12: pause the stock UI, run DOOM, restore the UI.
# Usage: sh doom_run.sh <dir>   (<dir> holds fbdoom, mcu_keepalive, doom1.wad)
D=${1:-$(dirname "$0")}
R=/tmp/f7doom
WD=7E0000000000021A01A3C77C     # MCU watchdog frame (glink 0x1A)
export LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/lib:/application/lib
export PATH=$PATH:/usr/local/bin

mkdir -p "$R"
exec >"$R/doom_run.log" 2>&1
echo "=== $(date) launch from $D"

# The stick is vfat (may be noexec): run the binaries from tmpfs.
for f in fbdoom mcu_keepalive; do
	cmp -s "$D/$f" "$R/$f" || cp "$D/$f" "$R/$f"
	chmod +x "$R/$f"
done

AP=$(pgrep -f siba/bin/appman)
[ -n "$AP" ] || { echo "appman not running; refusing"; exit 1; }

# appman feeds the MCU watchdog; while it's paused we must, or the MCU
# power-resets the SoC within ~10s.
"$R/mcu_keepalive" /dev/ttyS1 115200 3000 $WD >"$R/ka.log" 2>&1 &
KA=$!
sleep 1

# Save the UI's framebuffer (both pages) and which page is showing.
PAN=$(cat /sys/class/graphics/fb0/pan)
dd if=/dev/fb0 of="$R/fb.save" bs=1536000 count=2 2>/dev/null

restore() {
	kill "$HB" 2>/dev/null
	echo "=== $(date) DOOM exited, restoring" >> "$L"; sync
	pkill -f doom_snd.wav 2>/dev/null           # DOOM's as_track player
	dd if="$R/fb.save" of=/dev/fb0 bs=1536000 count=2 2>/dev/null
	echo "$PAN" > /sys/class/graphics/fb0/pan
	kill -CONT "$AP"
	echo "appman resumed; holding watchdog until it takes over"
	sleep 12
	kill "$KA" 2>/dev/null
	echo "=== $(date) restored"
}
trap 'restore; exit 0' INT TERM

kill -STOP "$AP"
echo "appman $AP paused (pan was $PAN)"

# Heartbeat to the stick (survives a reset, unlike /tmp): find out what dies.
L="$D/run.log"
echo "=== $(date) launch (uptime $(cut -d' ' -f1 /proc/uptime))" >> "$L"
( while :; do
	{ echo "--- $(cut -d' ' -f1 /proc/uptime)s ka=$(kill -0 $KA 2>/dev/null && echo up || echo DEAD)" \
	       "appman=$(grep State /proc/$AP/status 2>/dev/null | cut -f2)" \
	       "doom=$(pgrep -f /tmp/f7doom/fbdoom >/dev/null && echo up || echo -)" \
	       "free=$(grep MemFree /proc/meminfo | tr -s ' ' | cut -d' ' -f2)"
	} >> "$L"
	sync; sleep 5
  done ) &
HB=$!

mkdir -p "$D/save"
cd "$R" && HOME="$D/save" "$R/fbdoom" -iwad "$D/doom1.wad" >"$D/doom.log" 2>&1
echo "fbdoom exited rc=$?"

restore
