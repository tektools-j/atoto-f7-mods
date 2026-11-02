# FLASH_AND_RECOVERY

How to flash an image to the unit, and how to recover a boot-looping / bricked unit.
**Read this fully before you flash anything.** Flashing is the irreversible step; this
project boot-looped a unit twice before the repack tool was fixed (see
[README](README.md#4-permanent-flash--the-chunk-hash-gotcha)).

## Golden rules

1. **Keep a verified clean recovery image** (`ISPBOOOT.BIN` = your model's official
   firmware) on a separate stick/SD *before* you touch anything. Know its checksum.
2. **Stable power during the whole flash.** No engine-off, no yanked cables.
3. **Always RAM-overlay-test** your library change first (`scripts/overlay_load.sh`) — it
   reverts on reboot. Only flash once the overlay works on-screen.
4. **Verify the on-stick image checksum** after writing — a bad USB copy bricks the same
   as a bad build.

## Making a flash stick

1. Format a USB stick (or SD card) as **FAT32**.
2. Copy your image to the **root** of the stick, named exactly **`ISPBOOOT.BIN`**
   (no subfolder, exact case). The bootloader looks for that filename.
3. **Re-checksum the file on the stick** (`md5sum`/`sha256sum`) and compare to the
   source. Only proceed on a match.
4. A debug stick is different: it carries `debugnet.sh` at root and **no** `ISPBOOOT.BIN`
   (so a normal boot can't be mistaken for a flash). Don't mix the two.

## Flashing (forced-ISP)

1. Insert the flash stick. Ensure **stable power**.
2. Enter forced-ISP: **hold HOME + press reset**.
3. Let the ISP updater run to its **version/progress screen** and finish — **do not
   interrupt power.** It can take a few minutes.
4. When it signals done, **remove the stick**, then do a **plain reset** (no HOME).
5. The unit boots. Verify the result (for the dock mod: the reordered/reskinned dock;
   for a debug build: a debug stick brings up the `:2323` shell).

## Recovery (boot-loop or brick)

If the unit boot-loops, hangs, or won't come up after a flash:

1. **Don't panic and don't keep power-cycling** — put the **known-good recovery
   `ISPBOOOT.BIN`** (your model's official firmware) on a FAT32 stick root.
2. Insert it, ensure **stable power**, and do the same **forced-ISP** (HOME + reset).
3. Let it flash to completion, remove media, plain reset. The unit returns to stock.
4. If USB isn't detected, try the **microSD slot** — the ISP reads mmc too.
5. If forced-ISP itself won't engage, consult your model's community for the exact
   button timing / an alternate recovery entry; power stability is the usual culprit.

### Why flashes boot-loop here (and how to avoid it)

The ISP **verify** reads each partition back in fixed-size **chunks**, each with its own
checksum, and aborts on any mismatch. A repack tool that only re-syncs the *first* chunk
checksum will flash but **fail verify on a later chunk → boot-loop** whenever your change
lands past chunk 0 (as squashfs changes do). Changes that fit entirely in chunk 0 (e.g. a
boot logo) survive a buggy tool — a misleading success. **Ensure your repack re-hashes
every chunk** (see [REPACK.md](REPACK.md)), and always keep the recovery image handy.

> A build that is **"recovery base + your hook"** is a useful safety net: it boots like
> stock but keeps debug-shell access, so you can iterate via overlay without re-bootstrapping.
