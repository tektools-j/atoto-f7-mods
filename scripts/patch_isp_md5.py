#!/usr/bin/env python3
"""Re-sync the ISP script md5 integrity hashes after modifying partitions in ISPBOOOT.BIN.

Why: the embedded U-Boot ISP flash script (one per boot medium: nand @0x5d97000,
emmc @0x5da3000) runs a VERIFY phase that, for each partition, reads it back from
flash and compares md5(readback) against a hardcoded hash. If you modify any
partition, its md5 no longer matches -> `exit -1` -> the flash never finalizes ->
boot-loop. (Discovered the hard way, 2026-10-25.)

There is NO cryptographic signature -- the expected md5 is plaintext in the script,
and md5(partition bytes in this file) == the expected hash. So after editing
partition data, we recompute each hash from the file and rewrite it in place
(md5 hex is always 32 chars -> same length -> script size unchanged), then fix the
script's uImage CRC32s (dcrc over data, hcrc over header).

CRITICAL (fixed 2026-11-01): a partition is verified in MULTIPLE sequential chunks,
each `md5sum $isp_ram_addr <len> md5sum_value` + `if test ...` over its own slice of
the partition (e.g. spapp = ten 0x200000 chunks + one 0x18b000 remainder = the full
0x158b000 slot). The earlier version of this tool rewrote only the FIRST chunk hash
per partition, leaving later chunks stale -> any change past the first 2MB (exactly
the spapp dock/reskin mod) failed verify on a later chunk -> boot-loop even though the
image was otherwise correct. This version rewrites EVERY chunk hash, walking the file
offset forward by each chunk's own length starting at the partition's programming
fatload offset.

Usage:
    python3 tools/patch_isp_md5.py --in modified.bin [--out patched.bin] [--inplace]
Prints every (partition, chunk) whose hash changed. Idempotent.
"""
import argparse, hashlib, re, struct, sys, zlib, shutil

SCRIPT_OFFSETS = [0x5d97000, 0x5da3000]  # nand, emmc main ISP scripts (uImage)
UIMG_HDR = 0x40

FATLOAD_RE = re.compile(r'fatload\s+\S+\s+\S+\s+\S+\s+/ISPBOOOT\.BIN\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)')
PROG_RE    = re.compile(r'^echo programming (\S+)', re.M)
VERIFY_RE  = re.compile(r'^echo verifying (\S+)', re.M)
# One verify chunk: md5sum over <len>, then the expected hash. Groups: (len, hash).
CHUNK_RE   = re.compile(
    r'md5sum \$isp_ram_addr (0x[0-9a-fA-F]+) md5sum_value\s+'
    r'if test "\$md5sum_value" = ([0-9a-fA-F]{32})')


def parse_blocks(text, header_re):
    """Yield (name, start, end) spans delimited by lines matching header_re."""
    hits = list(header_re.finditer(text))
    for i, m in enumerate(hits):
        end = hits[i+1].start() if i+1 < len(hits) else len(text)
        yield m.group(1), m.start(), end


def patch_script(buf, soff, filebytes):
    """Patch ALL per-chunk md5 hashes + CRCs of the uImage script at soff in bytearray buf.
    Returns list of (name, chunk_index, old, new)."""
    magic, hcrc, t, size, load, ep, dcrc = struct.unpack('>IIIIIII', bytes(buf[soff:soff+28]))
    if magic != 0x27051956:
        return []  # no script here (e.g. emmc copy absent)
    dstart = soff + UIMG_HDR
    data = bytes(buf[dstart:dstart+size])
    text = data.decode('latin-1')  # 1:1 byte<->char so offsets are exact

    # name -> partition file offset, from the programming phase's FIRST fatload
    prog_off = {}
    for name, s, e in parse_blocks(text, PROG_RE):
        fm = FATLOAD_RE.search(text, s, e)
        if fm:
            prog_off[name] = int(fm.group(2), 16)  # group2 = file position

    changes = []
    repls = []  # (hash_start, hash_end, newhash) absolute positions in `text`
    # verify phase: walk every chunk of every partition, advancing the file offset
    for name, s, e in parse_blocks(text, VERIFY_RE):
        off = prog_off.get(name)
        if off is None:
            continue  # e.g. env (no md5 / no programming offset)
        k = 0
        for m in CHUNK_RE.finditer(text, s, e):
            size_v = int(m.group(1), 16)
            oldhash = m.group(2).lower()
            newhash = hashlib.md5(filebytes[off:off+size_v]).hexdigest()
            if newhash != oldhash:
                changes.append((name, k, oldhash, newhash))
                repls.append((m.start(2), m.end(2), newhash))
            off += size_v
            k += 1

    if not changes:
        return []

    # apply hash replacements right-to-left so positions stay valid (length-preserving anyway)
    new_text = text
    for a, b, nh in sorted(repls, reverse=True):
        new_text = new_text[:a] + nh + new_text[b:]

    new_data = new_text.encode('latin-1')
    assert len(new_data) == size, (len(new_data), size)
    buf[dstart:dstart+size] = new_data
    # recompute CRCs
    new_dcrc = zlib.crc32(new_data) & 0xffffffff
    struct.pack_into('>I', buf, soff + 0x18, new_dcrc)
    hdr = bytearray(buf[soff:soff+UIMG_HDR]); hdr[4:8] = b'\x00\x00\x00\x00'
    new_hcrc = zlib.crc32(bytes(hdr)) & 0xffffffff
    struct.pack_into('>I', buf, soff + 0x04, new_hcrc)
    return changes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--in', dest='inp', required=True)
    ap.add_argument('--out')
    ap.add_argument('--inplace', action='store_true')
    a = ap.parse_args()
    if not a.out and not a.inplace:
        sys.exit("specify --out FILE or --inplace")
    out = a.inp if a.inplace else a.out
    if out != a.inp:
        shutil.copyfile(a.inp, out)
    buf = bytearray(open(out, 'rb').read())
    filebytes = bytes(buf)  # snapshot of partition data (scripts don't overlap partitions)
    total = 0
    for soff in SCRIPT_OFFSETS:
        ch = patch_script(buf, soff, filebytes)
        if ch:
            print(f"script @ {soff:#x}:")
            for name, k, o, n in ch:
                print(f"  {name:12s} chunk{k:<2d} {o} -> {n}")
            total += len(ch)
    if total == 0:
        print("no hashes needed changing (image already consistent)")
    else:
        open(out, 'wb').write(bytes(buf))
        print(f"patched {total} chunk hash(es); wrote {out}")


if __name__ == '__main__':
    main()
