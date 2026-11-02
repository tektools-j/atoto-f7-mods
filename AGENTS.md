# AGENTS.md — runbook for an AI agent reproducing this mod

This file is written so a user can hand this repo to a capable coding agent and have it
carry out the ATOTO F7 home-dock customization **safely**. Read it top to bottom before
acting. `AGENTS.md`/`CLAUDE.md` conventions apply: treat the HARD RULES as binding.

## What this accomplishes

Reorder and reskin the fixed home-screen dock of an ATOTO F7 head unit by patching
`libhome.so`, validated in RAM first, then (optionally) flashed permanently.

## HARD RULES (do not violate)

1. **Never flash the unit without the owner's explicit, per-step approval.** Writing to
   the device is irreversible and can brick it. Reading, disassembly, and RAM-overlay
   tests are fine; flashing is a STOP-and-ask gate every time.
2. **Confirm a verified clean recovery image exists before any flash** (see
   [FLASH_AND_RECOVERY.md](FLASH_AND_RECOVERY.md)). No recovery image → do not flash.
3. **Always RAM-overlay-test a library change before flashing it** (reverts on reboot).
4. **Never redistribute vendor firmware/binaries/logo.** Work only on images the owner
   already has for their own device.
5. **Verify every artifact by checksum** at every step; abort on any mismatch.
6. **Offsets here are build-specific.** Disassemble the owner's actual `libhome.so` and
   confirm each offset — never blind-patch a different build.

## Inputs to collect from the user first

- Their **exact model** and **firmware version**, and their **official `ISPBOOOT.BIN`**
  for it (for extraction + recovery). Confirm its checksum.
- **Physical access**: ability to insert USB/SD and enter forced-ISP (HOME+reset), and
  **stable power** for the unit.
- A **workstation** with the tools in [PREREQUISITES.md](PREREQUISITES.md).
- Confirmation the user **accepts the brick/warranty risk** (see
  [DISCLAIMER.md](DISCLAIMER.md)).

## Procedure (each phase has a checkpoint; ⛔ = stop and get explicit approval)

**Phase 0 — Recovery first.**
Obtain the user's official `ISPBOOOT.BIN`, verify its checksum, stage it on a separate
FAT32 stick. This is the undo button. ✔ Checkpoint: recovery image in hand & verified.

**Phase 1 — Extract.**
Unpack the stock `ISPBOOOT.BIN` → partitions → the rootfs and app (`spapp`) squashfs →
`libhome.so`. See [REPACK.md](REPACK.md). ✔ Checkpoint: stock `libhome.so` checksum
recorded.

**Phase 2 — Bootstrap the debug hook ⛔ (flash).**
Offline: unpack the **rootfs** squashfs, append the `sys_setup.sh` hook (see
[README §1](README.md#1-root-shell-via-usb-debug-stick)), repack rootfs, re-sync **all**
ISP chunk checksums (see [REPACK.md](REPACK.md)) → a "hook" image. ⛔ Get approval, then
flash it (forced-ISP). ✔ Checkpoint: boots normally.
*(Skip Phase 2 if the unit already has shell access.)*

**Phase 3 — Shell.**
Put `scripts/debugnet.sh` at the root of a FAT32 stick (no `ISPBOOOT.BIN` on it), boot
normally, join the unit's own Wi-Fi hotspot, connect with `scripts/sh.ps1` (or `nc`) to
`:2323`. ✔ Checkpoint: `id` returns `uid=0(root)`.

**Phase 4 — Confirm offsets.**
Disassemble the owner's `libhome.so`; locate the dock `.bss` table, the populating
constructor, `initButtonUi`, and the `setDisabled` sites. Confirm the offsets in
[PATCH.md](PATCH.md) match this build (same stock checksum ⇒ they do). ✔ Checkpoint:
offsets verified or re-derived.

**Phase 5 — RAM overlay test (safe, reverts on reboot).**
Run `scripts/overlay_load.sh` on the unit (adjust offsets/PNG as needed). Prove the patch
loaded via `/proc/<pid>/maps` + a checksum of the plugin path, and **have the user
confirm the dock on screen**. Iterate here freely — nothing is flashed. ✔ Checkpoint:
new dock renders correctly on-screen.

**Phase 6 — Permanent flash ⛔.**
Offline: apply the patch to `libhome.so` (`scripts/build_patch.py`), repack the `spapp`
squashfs, splice into the ISP image, re-sync **all** chunk checksums → mod image. Prefer a
"recovery-base + hook + mod" image so shell access survives. **Verify by loop-mounting the
repacked squashfs on the unit** (`mount -t squashfs -o loop`) before flashing. ⛔ Get
approval, write to stick, re-checksum on-stick, flash (forced-ISP). ✔ Checkpoint: boots to
the new dock.

**Phase 7 — Verify & document.**
Confirm the result, confirm recovery image still works as the fallback, record final
checksums.

## If anything goes wrong

Boot-loop or brick → **do not keep power-cycling**; flash the recovery image via
forced-ISP (see [FLASH_AND_RECOVERY.md](FLASH_AND_RECOVERY.md)). A boot-loop after a
*flash that passed* almost always means the repack re-synced only chunk 0 — fix the
repack to re-hash **every** chunk.

## File map

`README.md` method · `PATCH.md` offsets · `PREREQUISITES.md` tools · `REPACK.md`
unpack/repack/flash-image build · `FLASH_AND_RECOVERY.md` flashing + recovery ·
`scripts/` the tooling · `DISCLAIMER.md` safety/legal.
