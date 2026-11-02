#!/bin/sh
# overlay_load.sh - RAM-only test of a patched libhome.so on the unit. NOTHING is flashed;
# a reboot restores stock. Run this ON THE UNIT via the :2323 debug shell.
#
# It snapshots the UI process environment, kills the UI, builds the patched library in
# /tmp from the stock one (the 19-byte dock patch + an optional radio-PNG splice), mounts
# a tmpfs overlay carrying it over the read-only plugin dir, and relaunches the UI.
# Checksum gates abort safely (restoring the overlay) on any mismatch.
#
# Offsets/checksums below are for firmware SOC F7-SOC5P-230609 (see PATCH.md) - adjust
# for your build. Optional: put a padded 23688-byte radio PNG on the stick to reskin.
# MIT licensed. Use at your own risk; see DISCLAIMER.md.
set +e

PLUGINS=/application/siba/plugins          # overlay target (ro squashfs)
SQUASH_PATH=/tmp/sp/application/siba/plugins  # same data, used to drop any prior overlay
STOCK_MD5=01e2addcfa9b0b135cdc9d357212bb6e
DOCK_MD5=a3ee707e8e32b373799e9714c8dbb4a0
RADIO_PNG=/mnt/sda1/radio_padded.png       # optional; 23688 bytes. Comment out to skip.

AP=$(pidof appman)
cat /proc/$AP/environ | tr '\0' '\n' | grep -v '^$' > /tmp/ui.env.txt
mkdir -p /tmp/ovl/up /tmp/ovl/wk

kill $AP 2>/dev/null
n=0; while pidof appman >/dev/null 2>&1 && [ $n -lt 25 ]; do usleep 200000; n=$((n+1)); done
umount "$SQUASH_PATH" 2>/dev/null
remount(){ mount -t overlay overlay -o lowerdir=$PLUGINS,upperdir=/tmp/ovl/up,workdir=/tmp/ovl/wk $PLUGINS; }

STOCK=$(md5sum $PLUGINS/libhome.so | cut -d' ' -f1)
[ "$STOCK" = "$STOCK_MD5" ] || { echo "ABORT: not stock ($STOCK)"; remount; exit 1; }
cp $PLUGINS/libhome.so /tmp/base.so

BAD=0
patch(){ cur=$(dd if=/tmp/base.so bs=1 skip=$1 count=1 2>/dev/null | od -An -tx1 | tr -d ' ')
  [ "$cur" = "$2" ] || { echo "MISMATCH @$1 have=$cur want=$2"; BAD=1; return; }
  printf "$3" | dd of=/tmp/base.so bs=1 seek=$1 count=1 conv=notrunc 2>/dev/null; }
# dispatch ids
patch 164590 0b '\012'; patch 164690 04 '\006'; patch 164722 0a '\011'
# icon pointers
patch 164812 ee '\032'; patch 164813 b9 '\272'
patch 164824 e6 '\242'; patch 164825 b9 '\265'
patch 164828 96 '\052'; patch 164829 b9 '\266'
# enable slot 4
patch 155924 19 '\000'; patch 155925 46 '\041'
patch 159546 19 '\000'; patch 159547 46 '\041'
patch 159970 19 '\000'; patch 159971 46 '\041'
# enable slot 0
patch 156200 01 '\000'; patch 159944 01 '\000'; patch 160862 01 '\000'; patch 161036 01 '\000'
[ "$BAD" = 0 ] || { echo "ABORT patch"; remount; exit 1; }
[ "$(md5sum /tmp/base.so | cut -d' ' -f1)" = "$DOCK_MD5" ] || { echo "ABORT: dock md5"; remount; exit 1; }

# Optional in-place PNG reskin at offset 0x7872a (493354), length 23688 -> [493354,517042)
if [ -r "$RADIO_PNG" ]; then
  head -c 493354 /tmp/base.so > /tmp/new.so
  cat "$RADIO_PNG" >> /tmp/new.so
  tail -c +517043 /tmp/base.so >> /tmp/new.so
  [ "$(wc -c < /tmp/new.so)" = 934388 ] || { echo "ABORT: size"; remount; exit 1; }
else
  cp /tmp/base.so /tmp/new.so
fi
echo "patched lib md5: $(md5sum /tmp/new.so | cut -d' ' -f1)"

cp /tmp/new.so /tmp/ovl/up/libhome.so; chmod 755 /tmp/ovl/up/libhome.so
remount; echo "overlay md5 now: $(md5sum $PLUGINS/libhome.so | cut -d' ' -f1)"

# Relaunch the UI from its saved environment, detached from this shell.
setsid /bin/sh -c 'cd /; while IFS= read -r l; do [ -n "$l" ] && export "$l"; done < /tmp/ui.env.txt; exec /application/siba/bin/appman --dfb:no-layers-clear' >/tmp/ui.log 2>&1 </dev/null &
echo "UI relaunched (screen blanks ~15s). Verify: cat /proc/\$(pidof appman)/maps | grep libhome"
