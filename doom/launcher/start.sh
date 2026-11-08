#!/bin/sh
# Called from the USB stick's debugnet.sh at boot: start the F12 DOOM launcher.
D=$(cd "$(dirname "$0")" && pwd)
cp "$D/f7doomd" /tmp/f7doomd && chmod +x /tmp/f7doomd
pkill -f /tmp/f7doomd 2>/dev/null
/tmp/f7doomd "$D" >/tmp/f7doomd.log 2>&1 &
