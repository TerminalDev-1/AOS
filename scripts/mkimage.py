#!/usr/bin/env python3
"""Build the bootable WinKit floppy image (out/winkit.img).

Layout of the 1.44 MB disk, written byte-for-byte by this script:

    sector 0          boot sector (BOOT.ASM): BPB + loader
    sectors 1..K      the kernel (KERNEL.BIN), as FAT "reserved" sectors
    FAT12 volume      2 FAT copies, 224 root entries, 512-byte clusters,
                      holding README.TXT and the .WKX programs

There is no DOS on the disk: the boot sector loads the kernel directly and
the kernel reads and writes the FAT12 volume itself (FS.INC).

Usage: python scripts/mkimage.py [--out out/winkit.img]
"""
import argparse
import os
import struct

from build import build_all

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out")

SECTORS = 2880
FAT_SECTORS = 9
ROOT_ENTRIES = 224
ROOT_SECTORS = ROOT_ENTRIES * 32 // 512
MAX_KERNEL = 120                      # sectors; the loader stops at 64 KB

BOOT_RSVD = 14                        # offsets inside the boot sector
BOOT_KSECTS = 62
DATE_1980_01_01 = 0x0021
ATTR_ARCHIVE = 0x20
ATTR_SYSTEM = 0x04                    # part of WinKit: no desktop shortcut
SHORTCUTS = {"SETUP.WKX"}             # shipped files that do get one

README = (b"WinKit-preview\r\n"
          b"==============\r\n"
          b"\r\n"
          b"This disk boots straight into the WinKit desktop; there is no DOS.\r\n"
          b"\r\n"
          b"Open the WinKit terminal from the Start menu or with F6 and try:\r\n"
          b"  dir\r\n"
          b"  type README.TXT\r\n"
          b"  hello\r\n"
          b"  count\r\n"
          b"  echo hi there > NOTE.TXT\r\n")


def name83(name):
    base, _, ext = name.upper().partition(".")
    if not (0 < len(base) <= 8 and len(ext) <= 3):
        raise ValueError(f"not an 8.3 name: {name}")
    return base.ljust(8).encode("ascii") + ext.ljust(3).encode("ascii")


def build_image(kernel, boot, files):
    ksects = (len(kernel) + 511) // 512
    if ksects > MAX_KERNEL:
        raise SystemExit(f"kernel is {len(kernel)} bytes; the loader handles "
                         f"at most {MAX_KERNEL * 512}")
    rsvd = 1 + ksects
    image = bytearray(SECTORS * 512)

    sector0 = bytearray(boot.ljust(512, b"\0"))
    assert sector0[510:512] == b"\x55\xaa", "boot sector lost its signature"
    struct.pack_into("<H", sector0, BOOT_RSVD, rsvd)
    struct.pack_into("<H", sector0, BOOT_KSECTS, ksects)
    image[0:512] = sector0
    image[512:512 + len(kernel)] = kernel

    fat_lba = rsvd
    root_lba = fat_lba + 2 * FAT_SECTORS
    data_lba = root_lba + ROOT_SECTORS
    clusters = SECTORS - data_lba

    fat = [0] * (FAT_SECTORS * 512 * 2 // 3)
    fat[0], fat[1] = 0xFF0, 0xFFF
    entries = bytearray()
    next_cluster = 2
    for name, data in files:
        need = (len(data) + 511) // 512
        if next_cluster + need > clusters + 2:
            raise SystemExit("image is full")
        first = next_cluster if need else 0
        for i in range(need):
            c = next_cluster + i
            fat[c] = c + 1 if i < need - 1 else 0xFFF
            lba = data_lba + c - 2
            chunk = data[i * 512:(i + 1) * 512]
            image[lba * 512:lba * 512 + len(chunk)] = chunk
        next_cluster += need
        entry = bytearray(32)
        entry[0:11] = name83(name)
        entry[11] = ATTR_ARCHIVE
        if name.upper() not in SHORTCUTS:
            entry[11] |= ATTR_SYSTEM
        struct.pack_into("<H", entry, 16, DATE_1980_01_01)
        struct.pack_into("<H", entry, 24, DATE_1980_01_01)
        struct.pack_into("<H", entry, 26, first)
        struct.pack_into("<I", entry, 28, len(data))
        entries += entry
    if len(entries) > ROOT_ENTRIES * 32:
        raise SystemExit("too many files for the root directory")

    raw = bytearray(FAT_SECTORS * 512)
    for i in range(0, len(fat) - 1, 2):
        a, b = fat[i], fat[i + 1]
        raw[i * 3 // 2:i * 3 // 2 + 3] = bytes(
            [a & 0xFF, (a >> 8) | ((b & 0xF) << 4), b >> 4])
    for copy in range(2):
        start = (fat_lba + copy * FAT_SECTORS) * 512
        image[start:start + len(raw)] = raw
    image[root_lba * 512:root_lba * 512 + len(entries)] = entries
    return bytes(image), ksects, data_lba


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=os.path.join(OUT, "winkit.img"))
    args = ap.parse_args()

    built = build_all()
    kernel = built.pop("KERNEL.BIN")
    boot = built.pop("BOOT.BIN")
    files = [("README.TXT", README)] + sorted(built.items())
    image, ksects, data_lba = build_image(kernel, boot, files)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as fh:
        fh.write(image)
    print(f"[image] {args.out}: kernel {len(kernel)} bytes in {ksects} "
          f"sectors, {len(files)} files, volume at sector {ksects + 1}")


if __name__ == "__main__":
    main()
