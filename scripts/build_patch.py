#!/usr/bin/env python3
"""build_patch.py - apply the ATOTO F7 dock-reorder patch to YOUR OWN libhome.so.

This ships NO vendor code. It reads a libhome.so you already have, verifies it is the
exact stock build the offsets were derived from (by checksum), applies the documented
19-byte reorder/enable patch, and optionally splices a replacement radio-icon PNG.

Usage:
    python3 build_patch.py stock_libhome.so patched_libhome.so [--radio-png radio.png]

See PATCH.md for what every offset means. MIT licensed. Use at your own risk.
"""
import hashlib, sys, argparse

STOCK_MD5  = "01e2addcfa9b0b135cdc9d357212bb6e"
STOCK_SIZE = 934388
DOCK_MD5   = "a3ee707e8e32b373799e9714c8dbb4a0"   # after the 19-byte patch
PNG_OFF    = 0x7872a
PNG_LEN    = 23688

# (file_offset, expected_old_byte, new_byte) - see PATCH.md
EDITS = [
    # dispatch ids
    (0x282ee, 0x0b, 0x0a), (0x28352, 0x04, 0x06), (0x28372, 0x0a, 0x09),
    # normal-icon resource pointers (little-endian words)
    (0x283cc, 0xee, 0x1a), (0x283cd, 0xb9, 0xba),
    (0x283d8, 0xe6, 0xa2), (0x283d9, 0xb9, 0xb5),
    (0x283dc, 0x96, 0x2a), (0x283dd, 0xb9, 0xb6),
    # force-enable slot 4 (mov r1,r3 -> movs r1,#0)
    (0x26114, 0x19, 0x00), (0x26115, 0x46, 0x21),
    (0x26f3a, 0x19, 0x00), (0x26f3b, 0x46, 0x21),
    (0x270e2, 0x19, 0x00), (0x270e3, 0x46, 0x21),
    # force-enable slot 0 (movs r1,#1 -> movs r1,#0)
    (0x26228, 0x01, 0x00), (0x270c8, 0x01, 0x00),
    (0x2745e, 0x01, 0x00), (0x2750c, 0x01, 0x00),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stock");  ap.add_argument("out")
    ap.add_argument("--radio-png", help="padded replacement PNG, exactly %d bytes" % PNG_LEN)
    a = ap.parse_args()

    b = bytearray(open(a.stock, "rb").read())
    if len(b) != STOCK_SIZE or hashlib.md5(b).hexdigest() != STOCK_MD5:
        sys.exit("refusing: input is not the exact stock libhome.so these offsets target "
                 "(size %d md5 %s). Disassemble your own build and adjust PATCH.md." %
                 (STOCK_SIZE, STOCK_MD5))

    for off, old, new in EDITS:
        if b[off] != old:
            sys.exit("offset 0x%x: found 0x%02x, expected 0x%02x - aborting" % (off, b[off], old))
        b[off] = new
    inter = hashlib.md5(b).hexdigest()
    print("after 19-byte dock patch: md5 %s %s" %
          (inter, "OK" if inter == DOCK_MD5 else "!! unexpected"))

    if a.radio_png:
        png = open(a.radio_png, "rb").read()
        if len(png) != PNG_LEN:
            sys.exit("radio PNG must be exactly %d bytes (pad it); got %d" % (PNG_LEN, len(png)))
        b[PNG_OFF:PNG_OFF + PNG_LEN] = png
        print("spliced radio PNG at 0x%x" % PNG_OFF)

    if len(b) != STOCK_SIZE:
        sys.exit("size changed to %d - something is wrong, not writing" % len(b))
    open(a.out, "wb").write(b)
    print("wrote %s  size %d  md5 %s" % (a.out, len(b), hashlib.md5(b).hexdigest()))


if __name__ == "__main__":
    main()
