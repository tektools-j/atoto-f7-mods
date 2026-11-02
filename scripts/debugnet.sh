#!/bin/sh
# debugnet.sh - USB-stick payload for an ATOTO F7 (SPHE8368, Linux 4.9.217 / BusyBox).
# Brings up a root shell over the unit's own Wi-Fi hotspot.
#
# Place at the ROOT of a FAT32 USB stick as /debugnet.sh. A one-line hook appended to the
# rootfs /sbin/sys_setup.sh exec's it at boot (see README). On this box plain telnetd
# fails because rootfs devpts has ptmxmode=000 (pty-master open fails), so we mount a
# usable devpts, try telnetd, and fall back to a pty-free `nc` shell on TCP 2323.
#
# Contains NO credentials. The unit's hotspot SSID/password are read from the unit's own
# config at runtime and only logged locally to the stick for your convenience.
# MIT licensed. Use at your own risk; see DISCLAIMER.md.

HERE="$(dirname "$0")"; exec >"$HERE/debugnet.log" 2>&1; set -x
export LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/lib:/system/lib:/system/qt/lib:/application/lib:/apps/lib
export PATH=$PATH:/usr/local/bin:/application/bin:/apps/bin
sleep 15

# Log the active hotspot info (from the unit's OWN config) so you know where to connect.
{ cat /media/flash/nvm/softap.conf 2>/dev/null; /usr/local/bin/nw_control --gip wlan0 2>/dev/null; ifconfig wlan0; } > "$HERE/ap_info.txt" 2>&1

# Ensure a usable PTY for telnetd (the fix for the ptmxmode=000 problem).
grep -q devpts /proc/filesystems && echo "devpts supported" || echo "devpts NOT supported"
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts -o rw,mode=620,ptmxmode=666 2>/dev/null \
  || mount -o remount,rw,mode=620,ptmxmode=666 -t devpts devpts /dev/pts 2>/dev/null
[ -e /dev/ptmx ] || mknod /dev/ptmx c 5 2 2>/dev/null
chmod 666 /dev/ptmx /dev/pts/ptmx 2>/dev/null

# Attempt 1: telnetd on :23.
telnetd -F -l /bin/sh -p 23 & TPID=$!
sleep 2
if netstat -tln 2>/dev/null | grep -q ':23 '; then
  echo "telnetd listening on :23"
else
  kill "$TPID" 2>/dev/null
  telnetd -l /bin/sh -p 23; sleep 2
  if netstat -tln 2>/dev/null | grep -q ':23 '; then
    echo "telnetd (daemon) listening on :23"
  else
    # Fallback: pty-free shell via nc + fifo, respawned per connection, on :2323.
    echo "telnetd failed; starting pty-free nc shell on :2323"
    rm -f /tmp/sh.fifo; mkfifo /tmp/sh.fifo 2>/dev/null
    ( while true; do cat /tmp/sh.fifo | /bin/sh -i 2>&1 | nc -l -p 2323 > /tmp/sh.fifo; done ) &
    sleep 2
    netstat -tln 2>/dev/null | grep -q ':2323 ' \
      && echo "pty-free nc shell listening on :2323" \
      || echo "NOTHING listening (telnetd + nc both failed)"
  fi
fi

echo "--- final listening state ---"
ps 2>/dev/null | grep -iE '[t]elnetd|[n]c ' ; netstat -tlnp 2>/dev/null | grep -E ':23 |:2323 '
echo "=== debugnet done ==="
