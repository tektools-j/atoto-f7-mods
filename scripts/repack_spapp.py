#!/usr/bin/env python3
"""Splice a rebuilt spapp SquashFS into a copy of ISPBOOOT.BIN (offline; no device).

The spapp (UI app, sqfs4) partition occupies a fixed slot in ISPBOOOT.BIN. Replacing just
this slot — padded to the original slot length — keeps every other region byte-identical, so
no offsets, CRCs, or the embedded ISP scripts need changing. See REPACK.md.
"""
import argparse, hashlib, struct, sys

SPAPP_OFFSET = 0x365e000
SPAPP_SLOT   = 0x158b000  # programmed size of the spapp slot

def sha(b): return hashlib.sha256(b).hexdigest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True, help="original ISPBOOOT.BIN")
    ap.add_argument("--sqfs",  required=True, help="rebuilt spapp .sqfs to splice in")
    ap.add_argument("--out",   required=True, help="output ISPBOOOT.BIN")
    ap.add_argument("--offset", type=lambda x: int(x,0), default=SPAPP_OFFSET)
    ap.add_argument("--slot",   type=lambda x: int(x,0), default=SPAPP_SLOT)
    a = ap.parse_args()

    img = bytearray(open(a.image, "rb").read())
    new = open(a.sqfs, "rb").read()

    if new[0:4] != b"hsqs":
        sys.exit(f"ERROR: {a.sqfs} is not SquashFS (magic={new[0:4]!r})")
    if len(new) > a.slot:
        sys.exit(f"ERROR: new sqfs {len(new)} (0x{len(new):x}) exceeds slot "
                 f"{a.slot} (0x{a.slot:x}) by {len(new)-a.slot} bytes")
    if a.offset + a.slot > len(img):
        sys.exit("ERROR: slot extends past end of image")

    before = bytes(img[a.offset:a.offset+a.slot])
    # write new sqfs then zero-pad to the full slot length
    img[a.offset:a.offset+a.slot] = new + b"\x00" * (a.slot - len(new))
    open(a.out, "wb").write(img)

    # report
    print(f"image size      : {len(img)} (0x{len(img):x})")
    print(f"slot            : offset=0x{a.offset:x} len=0x{a.slot:x}")
    print(f"new spapp sqfs  : {len(new)} bytes (0x{len(new):x}), {len(new)/1024/1024:.2f} MB, "
          f"slack={a.slot-len(new)} bytes")
    print(f"spapp sha256 old: {sha(before)}")
    print(f"spapp sha256 new: {sha(bytes(img[a.offset:a.offset+a.slot]))}")
    print(f"wrote           : {a.out}")

if __name__ == "__main__":
    main()
