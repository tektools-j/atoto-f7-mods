# PATCH.md — dock table structure & byte offsets

Reference for the `libhome.so` build shipped in ATOTO firmware `SOC F7-SOC5P-230609`.

- **Stock `libhome.so`:** size `934388` bytes, MD5 `01e2addcfa9b0b135cdc9d357212bb6e`.
- In this build, **ELF file offset == virtual address** for the sections of interest,
  so the offsets below are usable directly with `dd`/a hex editor.
- **Verify every offset against your own binary** (disassemble; don't trust a blind
  byte patch). A different firmware build *will* have different offsets.

> These are facts about where data sits, not ATOTO's code. You apply them to a
> `libhome.so` you already own. This file ships no binary.

## Dock data structure

The dock is 5 slots, drawn left→right. Each slot is a 16-byte entry in a `.bss` table
(at VA `0xebcc8` in this build), filled by a constructor reached via `init_array`:

| field  | offset in entry | meaning |
|--------|-----------------|---------|
| id     | `+0x0`          | source-screen dispatch id (what the tap launches) |
| label  | `+0x4`          | string id (dock shows no visible text, so cosmetic) |
| icon   | `+0x8`          | `QString` pointer to the normal icon resource path |
| enabled| `+0xc`          | initial enabled flag |

`initButtonUi` reads this table to set each icon's dispatch id and normal image.
`onIconClicked(id) → touched(id)` forwards the tap to the host framework; there is **no
whitelist** in `libhome` — any valid source id is forwarded (the final id→screen switch
lives in the host binary).

### Source-screen id map (from the app-grid constructor)

| id (hex) | screen |
|----------|--------|
| `0x0b` | Android Auto |
| `0x0a` | CarPlay |
| `0x03` | Radio |
| `0x64` | App grid / menus |
| `0x04` | USB |
| `0x06` | Rear camera |
| `0x29` | Front camera |
| `0x09` | Settings / setup |

### Stock dock vs. target

| slot | stock id | target id |
|------|----------|-----------|
| 0 | `0x0b` Android Auto | `0x0a` CarPlay |
| 1 | `0x03` Radio | `0x03` Radio (unchanged) |
| 2 | `0x64` App grid | `0x64` App grid (unchanged) |
| 3 | `0x04` USB | `0x06` Rear camera |
| 4 | `0x0a` CarPlay | `0x09` Settings |

## The 19-byte reorder/enable patch

Resulting MD5 after all 19 edits: `a3ee707e8e32b373799e9714c8dbb4a0` (size unchanged).

### a) Dispatch ids (3 bytes)

| file offset | old | new | effect |
|-------------|-----|-----|--------|
| `0x282ee` | `0b` | `0a` | slot 0 → CarPlay |
| `0x28352` | `04` | `06` | slot 3 → rear camera |
| `0x28372` | `0a` | `09` | slot 4 → Settings |

### b) Normal-icon resource pointers (6 bytes)

The constructor loads each slot's icon-path `QString` from a pool of little-endian
pointer words; point the three changed slots at the matching stock icon resource
(carplay / camera / setup PNGs already in the library's `.rodata`):

| file offset | old (LE) | new (LE) | icon |
|-------------|----------|----------|------|
| `0x283cc` | `ee b9` | `1a ba` | carplay |
| `0x283d8` | `e6 b9` | `a2 b5` | camera |
| `0x283dc` | `96 b9` | `2a b6` | setup |

### c) Force-enable the two device slots (10 bytes)

Slots 0 and 4 grey out (`setDisabled`) when Android Auto / CarPlay isn't connected.
Neutralize that so the repurposed slots are always tappable:

Slot 4 — `mov r1, r3` → `movs r1, #0` (`19 46` → `00 21`):

| file offset | old | new |
|-------------|-----|-----|
| `0x26114` | `19 46` | `00 21` |
| `0x26f3a` | `19 46` | `00 21` |
| `0x270e2` | `19 46` | `00 21` |

Slot 0 — `movs r1, #1` → `movs r1, #0` (`01` → `00`):

| file offset | old | new |
|-------------|-----|-----|
| `0x26228` | `01` | `00` |
| `0x270c8` | `01` | `00` |
| `0x2745e` | `01` | `00` |
| `0x2750c` | `01` | `00` |

See [`scripts/build_patch.py`](scripts/build_patch.py) for a reference implementation
that applies these with a stock-checksum guard.

## Optional: reskin an icon (PNG splice)

The icon PNGs are stored uncompressed in the library. You can replace one in-place as
long as the replacement is **padded to the exact same byte length** so the file size and
all later offsets are unchanged.

Worked example (this build): the normal radio icon PNG lives at file offset `0x7872a`
and is `23688` bytes. Overwrite exactly those `23688` bytes with your padded replacement
PNG (same dimensions, re-encoded/padded to `23688` bytes). File size stays `934388`.

Build-order that worked: stock → apply the 19 reorder/enable bytes → splice the PNG.
Keep a checksum of the final library and verify it both offline and on-device (via
`/proc/<pid>/maps`) before trusting it.
