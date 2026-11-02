# REPACK.md — unpack firmware, repack a squashfs, build a flashable image

How the permanent images are built: extract the firmware, modify a squashfs (rootfs for
the debug hook, or `spapp` for the dock mod), splice it back into the ISP image, and
re-sync the bootloader's verification checksums.

> **Specification derived from one firmware build (`SOC F7-SOC5P-230609`).** The exact
> partition offsets/sizes are **build-specific — derive them from your own
> `ISPBOOOT.BIN`** (the method below is self-describing, so you don't have to guess). No
> vendor binaries are included; you operate on firmware you already own.

## 1. The ISP image is self-describing

`ISPBOOOT.BIN` embeds two U-Boot scripts that *are* the installer:

- **init script** @ file `0x100020` — sets up region offsets and sources the main script.
- **main flasher** @ `0x5d97000` (NAND variant) / `0x5da3000` (eMMC variant).

Parse the main script and you get the whole partition map with no guessing — each
partition appears as a `fatload … /ISPBOOOT.BIN <len> <file_off>` followed by a
`nand write <ram> <nand> <len>`. The same script also carries the **verification block**
(see §4).

### Partition map (this build — derive yours by parsing the script)

| partition | file offset | programmed size | content |
|-----------|-------------|-----------------|---------|
| xboot1 | `0x104000` | `0x5000` | |
| uboot1 | `0x109000` | `0x8f000` | |
| uboot2 | `0x198000` | `0x8f000` | |
| ecos | `0x227000` | `0x26ec00` | uImage |
| kernel | `0x495c00` | `0x416400` | Linux uImage |
| **rootfs** | `0x8ac000` | `0x200000` | squashfs → `/` (mtd8) |
| opt | `0xaac000` | `0x3b000` | squashfs |
| spsdk | `0xae7000` | `0x2b77000` | squashfs → `/usr/local` (mtd10) |
| **spapp** | `0x365e000` | `0x158b000` | squashfs → `/application/siba` — the UI (mtd11) |
| logo | `0x4bef800` | `0x177400` | boot logo |
| version_info | `0x5d96400` | `0x400` | |

On-device cross-reference: `cat /proc/mtd` (rootfs=mtd8, spsdk=mtd10, spapp=mtd11,
nvm=mtd12, logo=mtd14, …).

### Identifying the squashfs

Magic `hsqs` at the partition's file offset; `unsquashfs` reads the real size from the
superblock. On this build the filesystems are **SquashFS v4.0, comp=lzo, block=32768,
flags=0x0440**.

## 2. Unpack

Carve each partition by (file offset, size) from the script, then `unsquashfs` the target
(rootfs or `spapp`) to get `libhome.so` / `sys_setup.sh`.

## 3. Modify

- **Debug hook (rootfs):** append the poll-for-`debugnet.sh` line to `/sbin/sys_setup.sh`
  (see [README §1](README.md#1-root-shell-via-usb-debug-stick)).
- **Dock mod (`spapp`):** replace `libhome.so` with the patched one
  (`scripts/build_patch.py`).

## 4. Repack the squashfs — match stock flags exactly

Use the vendor's exact compressor settings or the superblock flags won't match and you
risk a filesystem the unit mounts differently:

```
mksquashfs <tree> out.sqfs \
  -comp lzo -Xalgorithm lzo1x_999 -Xcompression-level 9 \
  -b 32768 -noappend -all-root -no-exports
```

That reproduces flags `0x0440` (byte-structurally like stock). **Omitting the `-X` lzo
options yields flags `0x0240`** — a subtle mismatch that wasted time in this project.
Assert `out.sqfs` size ≤ the slot (`0x158b000` for `spapp`).

**Prove the unit's own kernel mounts your output before going further:**
```
mount -t squashfs -o loop out.sqfs /mnt && md5sum /mnt/siba/plugins/libhome.so
```

## 5. Splice into the ISP image

`scripts/repack_spapp.py`: copy `ISPBOOOT.BIN`, overwrite the partition's slot with
`out.sqfs`, and **zero-pad to the full slot length** so every byte outside the slot is
unchanged. Raw squashfs partitions carry **no integrity field of their own**, so no other
offset/CRC edits are needed here — the integrity lives entirely in the ISP verify block
(§6). Defaults for this build: `--offset 0x365e000 --slot 0x158b000` (flags for other
slots, e.g. rootfs `--offset 0x8ac000 --slot 0x200000`).

## 6. Re-sync ALL verification checksums — the mandatory last step

**The ISP verify is per-chunk.** Each partition is read back in **`0x200000` chunks**
(final chunk = remainder), each guarded by its own `md5sum … ; if test … ; else exit -1`.
Chunk counts on this build:

| partition | chunks |
|-----------|--------|
| spapp | **11** (`10 × 0x200000` + `1 × 0x18b000`) |
| spsdk | 22 |
| rootfs / opt / logo | 1 each |

61 md5 chunks per script, **122 total** across the NAND + eMMC scripts. For each chunk:

```
hash_k = md5( file[ prog_off + Σ(prev chunk lens) : + len_k ] )
```

`scripts/patch_isp_md5.py` recomputes **every** chunk of **every** partition, rewrites any
changed hashes in **both** scripts, and fixes each uImage **CRC32** (`dcrc` @ `+0x18`,
header `hcrc` @ `+0x04`, big-endian; `hcrc` computed with its own field zeroed).

> **THE BUG that boot-looped this project twice:** the first version of the tool rewrote
> only the **first** chunk per partition. A `spapp` change lands in chunks 8–10, so those
> stale hashes failed verify → `exit -1` → boot-loop. Single-chunk partitions (logo,
> sub-2 MB rootfs) survived by luck. **Fix: re-hash every chunk.** Validate by
> recomputing all 122 chunk md5s — a correct build has **0 mismatches vs. a clean stock
> image.**

## 7. Verify before flashing

- Round-trip: re-extract your built image, diff the squashfs tree against your source
  (file count, symlinks resolve via `ls -lL`, `libhome.so` checksum).
- Loop-mount the built partition's squashfs **on the unit** (§4) — the one check a VM
  can't fully stand in for.
- Then flash: [FLASH_AND_RECOVERY.md](FLASH_AND_RECOVERY.md).

## Bootstrap (first shell from a stock unit) — fully offline

No prior access needed. Carve the rootfs squashfs (`0x8ac000`), unpack, append the
opt-in `sys_setup.sh` hook (polls `/mnt/*` for a marker script and execs it if present;
**no marker ⇒ normal boot, cannot boot-loop**), repack with the **same** `mksquashfs`
params, re-sync chunk md5s, flash via forced-ISP.

> **rootfs gotcha:** keep the repacked rootfs squashfs **under `0x200000` (2 MB)** so it
> stays a **single** verify chunk. If it grows past 2 MB it spans ≥2 chunks and you
> **must** use the fixed `patch_isp_md5.py` (the single-chunk-only version would brick
> it). Keep your marker/debug script and any Wi-Fi credentials **off the image and out of
> your repo.**

## Reference scripts

- `scripts/build_patch.py` — apply the dock patch to your `libhome.so` (included).
- `scripts/repack_spapp.py` — splice a repacked squashfs into the ISP image + zero-pad.
- `scripts/patch_isp_md5.py` — recompute **all** chunk md5s + uImage CRCs in both scripts.
