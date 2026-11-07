# ATOTO F7 — Home-Dock Customization (reverse-engineering writeup)

Reverse-engineering notes and tooling for customizing the **home-screen dock** on an
ATOTO F7 head unit — reordering the fixed dock icons and reskinning one of them — by
patching the UI library, validated safely in RAM before any permanent flash.

> **This repo contains only original research, offsets, and tooling. It does NOT
> contain any ATOTO firmware, libraries, images, or the vehicle boot logo.** You apply
> the documented patch to firmware *you already have on your own device*. See
> [DISCLAIMER.md](DISCLAIMER.md) before doing anything.

### Start here

- **Handing this to an AI agent?** → **[AGENTS.md](AGENTS.md)** — a safe, step-by-step
  runbook with approval gates and checkpoints.
- **Doing it yourself?** → [PREREQUISITES.md](PREREQUISITES.md) →
  [REPACK.md](REPACK.md) → this README → [PATCH.md](PATCH.md) →
  [FLASH_AND_RECOVERY.md](FLASH_AND_RECOVERY.md).
- **First, always:** read [DISCLAIMER.md](DISCLAIMER.md) and get a verified recovery
  image. This can brick your unit.

## TL;DR

The dock is five fixed icons drawn by `libhome.so` (a Qt/DirectFB plugin). Each slot's
`{id, label, icon, enabled}` lives in a small table in the library's `.bss`, populated by
a constructor at load time. By changing a handful of bytes you can:

- **reorder / repurpose** which source screen each slot launches (CarPlay, Radio, app
  grid, rear camera, Settings, …),
- **force-enable** the two slots that normally grey out when a phone isn't connected,
- **reskin** a slot's icon by splicing a replacement PNG over the stock one in-place.

All of this was proven on-device first via a **RAM overlay** (reverts on reboot, nothing
written to flash), and only then flashed permanently.

## Device

| | |
|---|---|
| Model | ATOTO F7-series (F7G2A7XE in my case) |
| SoC | Sunplus / SunMedia **SPHE8368** |
| OS | Embedded **Linux 4.9.217** (armv7l), BusyBox 1.29.3 — **not** Android |
| UI | Qt on **DirectFB**; home screen plugin = `libhome.so` under `/application/siba/plugins` |
| Firmware family | `SOC F7-SOC5P-230609` |

Offsets/hashes below are specific to that `libhome.so` build. Treat them as a worked
example of the *method*; verify against your own binary before use.

---

## Method overview

1. **Get a root shell** on the unit (serial-free): a USB "debug stick" whose
   `debugnet.sh` is launched at boot by a small hook, bringing up a shell over the
   unit's own Wi-Fi hotspot. See [docs below](#1-root-shell-via-usb-debug-stick).
2. **Reverse the dock** in `libhome.so` with a disassembler (I used Ghidra + capstone):
   find the slot table and the constructor that fills it. See
   [PATCH.md](PATCH.md) for the exact structure and byte offsets.
3. **Test in RAM first** with a tmpfs overlay over the read-only plugin directory, then
   relaunch the UI process. Reverts completely on reboot. See
   [`scripts/overlay_load.sh`](scripts/overlay_load.sh).
4. **Make it permanent** by repacking the modified library into the app partition image
   and flashing — *carefully*, because the bootloader verifies the partition in
   chunks. See [the flash gotcha](#4-permanent-flash--the-chunk-hash-gotcha).

---

## 1. Root shell via USB debug stick

The rootfs init (`/sbin/sys_setup.sh`) can be made to run a script off a USB stick at
boot. The hook is a single backgrounded line that polls for a marker file and never
blocks boot:

```sh
# appended to /sbin/sys_setup.sh
( n=0; while [ $n -lt 60 ]; do
    for d in /mnt/*; do
      if [ -r "$d/debugnet.sh" ]; then exec /bin/sh "$d/debugnet.sh" >"$d/debugnet.log" 2>&1; fi
    done
    n=$((n+1)); sleep 3
  done ) &
```

The stick carries [`scripts/debugnet.sh`](scripts/debugnet.sh), which starts a
**pty-free `netcat` root shell on TCP 2323** (plain `telnetd` fails on this box because
rootfs `devpts` is mounted with `ptmxmode=000`, so pty allocation fails). Join the
unit's own hotspot from a laptop and drive the shell with
[`scripts/sh.ps1`](scripts/sh.ps1) (Windows) or any `nc` client.

> **Note:** a *full firmware flash of stock firmware wipes this hook* (it lives in the
> rootfs partition). If you flash stock/recovery you'll lose the shell until you reflash
> a build that re-adds the hook. Plan accordingly.

## 2. Reversing the dock

The dock is five `cm::HomeIcon` widgets laid out with absolute geometry. Their data
(which screen each launches, which icon, whether enabled) is a table of 16-byte entries
in `.bss`, filled by an `init_array` constructor at load. `initButtonUi` reads it to set
each icon's dispatch id and normal image; disconnected-device slots are greyed by
`setDisabled` calls you can neutralize.

Full structure, the source-screen id map, and every byte offset are in
**[PATCH.md](PATCH.md)**.

## 3. RAM overlay test (safe, reverts on reboot)

The plugin dir is a read-only squashfs, but `/etc` on this unit already uses a tmpfs
overlay — proving tmpfs-as-upper works. So:

1. snapshot the UI process environment from `/proc/<pid>/environ`,
2. kill the UI, expose the stock library, build your patched copy in `/tmp` (verify its
   checksum!),
3. `mount -t overlay` a tmpfs upper over the plugin dir with your patched library,
4. relaunch the UI from the saved environment.

Proof-of-load: `/proc/<pid>/maps` + a checksum of the plugin path. The screen blanks for
~15 s during the relaunch. Nothing touches flash; a reboot restores stock. See
[`scripts/overlay_load.sh`](scripts/overlay_load.sh) — this is how you iterate without
risk.

## 4. Permanent flash — the chunk-hash gotcha

To persist, the patched library is repacked into the app partition's squashfs and
spliced into the ISP flash image. The **important** discovery: the bootloader's ISP
**verify** reads each partition back in **multiple fixed-size chunks**, each with its
**own checksum**, and aborts on any mismatch. The app partition on this unit verifies as
**11 chunks** (`10 × 0x200000` + `1 × 0x18b000`).

A naive repack tool that only re-syncs the *first* chunk's checksum per partition will
flash fine but **fail verify on a later chunk → boot-loop**, because the actual change
(in the squashfs tail) lands in chunks 8–10. The fix is simply to **recompute every
chunk checksum**. Changes small enough to fit entirely in chunk 0 (e.g. a boot logo)
survive the naive tool, which is a misleading early success.

Lesson: **always RAM-overlay-test the library, and verify the repack tool re-hashes all
chunks.** Two boot-loops here were 100% the repack tool, not the patch.

---

## Repo contents

```
README.md                 this file: device, method, the chunk-hash gotcha
AGENTS.md                 step-by-step runbook for an AI agent (safety gates, checkpoints)
PREREQUISITES.md          hardware, your-own-firmware, workstation + on-unit tools
PATCH.md                  dock table structure, source-id map, exact byte offsets
REPACK.md                 unpack firmware, repack a squashfs, build a flashable image
FLASH_AND_RECOVERY.md     how to flash, and how to recover a boot-looping/bricked unit
IOSMIRROR_YOUTUBE.md      field note: why the iPhone YouTube app won't mirror (and what does work)
DISCLAIMER.md             safety + legal; read first
scripts/
  debugnet.sh             USB-stick payload: brings up the :2323 nc root shell
  sh.ps1                  Windows driver to send a command batch over the :2323 shell
  build_patch.py          applies the dock-reorder byte patch to YOUR OWN libhome.so
  overlay_load.sh         on-device: build patched lib in RAM + overlay + relaunch UI
```

None of these ship any ATOTO code — `build_patch.py` reads *your* `libhome.so` and
writes a patched copy; it asserts the stock checksum so it refuses to run on anything but
the exact build it was derived from.

## Credits / license

Original tooling and notes are MIT-licensed (see headers). ATOTO, Sunplus, and any
vehicle-maker marks belong to their owners and nothing here is affiliated with or
endorsed by them.
