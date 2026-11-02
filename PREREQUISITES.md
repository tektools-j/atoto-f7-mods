# PREREQUISITES

What you need before starting. Offsets/params in this repo are for firmware
`SOC F7-SOC5P-230609`; a different build means re-deriving offsets (see PATCH.md).

## Hardware

- The **ATOTO F7 head unit** (this writeup: SPHE8368 SoC, Linux 4.9.217).
- **Two USB flash drives**, FAT32 (one "flash stick", one "debug stick"), and/or a
  microSD card — the unit's ISP reads USB *and* mmc/SD.
- **Stable power** for the unit during flashing: a bench supply or the car with the
  engine running. **A power cut mid-flash can brick it.**
- A **laptop/PC** with Wi-Fi (to join the unit's hotspot for the debug shell).

## Your own firmware (NOT included in this repo)

- The **official firmware image** (`ISPBOOOT.BIN`) for *your exact model*, downloaded
  from the vendor. You need it to (a) extract `libhome.so` and (b) have a **verified
  clean recovery image**. Record its checksum.
- **Do not use someone else's dumped firmware and do not redistribute the vendor's.**

## Workstation tools

| Tool | Use |
|------|-----|
| **Ghidra** (or IDA/radare2) | disassemble `libhome.so`, confirm the dock table + offsets |
| **Python 3** + `capstone` + `pyelftools` | scripted analysis; `build_patch.py` |
| a **hex editor** (or `dd`) | apply/inspect byte patches |
| **squashfs-tools** with **LZO** support (`mksquashfs`/`unsquashfs`) | unpack/repack the app & rootfs squashfs (this firmware uses `-comp lzo`) |
| **md5sum**/`sha256sum` | verify every artifact at every step |
| USB/SD imaging | write `ISPBOOOT.BIN` to the stick root |
| `nc` (netcat) or **PowerShell** | drive the on-unit debug shell (see `scripts/sh.ps1`) |

## On-unit tools (already present in the firmware's BusyBox)

`nc`, `losetup`, `mount` (squashfs + `-o loop`), `dd`, `od`, `md5sum`, `head`/`tail -c`,
`setsid`, `/proc` access as root. Verified present on this firmware.

## Knowledge / access

- How to enter **forced-ISP (firmware recovery) mode** on your model — typically
  **hold HOME + press reset** with the flash stick inserted. Know this *before* you start.
- Basic ARM Thumb disassembly reading (for confirming offsets against your own build).

> **Non-negotiable:** have a **verified clean recovery image in hand** and know how to
> flash it before you modify anything. See [FLASH_AND_RECOVERY.md](FLASH_AND_RECOVERY.md).
