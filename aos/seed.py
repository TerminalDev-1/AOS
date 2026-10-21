"""Create the native AOS FAT12 data volume used by UEFI and installed boots."""

import argparse
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "out"
SECTOR = 512
SECTORS = 2880
FAT_SECTORS = 9
ROOT_ENTRIES = 224
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR
DATA_SECTOR = 1 + 2 * FAT_SECTORS + ROOT_SECTORS
README = (b"AOS\r\n===\r\n\r\n"
          b"AOS is a 64-bit UEFI operating system.\r\n"
          b"Open Files, Notes, Write, Terminal or Programs from Start or the taskbar.\r\n"
          b"Terminal commands: help, dir, type NAME, ver, mem.\r\n")


def build_seed(path: Path):
    """Write a FAT12 volume with a BPB but no BIOS boot code."""
    image = bytearray(SECTORS * SECTOR)
    boot = memoryview(image)[:SECTOR]
    boot[:3] = b"\xeb\x3c\x90"
    boot[3:11] = b"AOS     "
    struct.pack_into("<H", boot, 11, SECTOR)
    boot[13] = 1  # sectors per cluster
    struct.pack_into("<H", boot, 14, 1)  # reserved sectors
    boot[16] = 2  # FAT copies
    struct.pack_into("<HH", boot, 17, ROOT_ENTRIES, SECTORS)
    boot[21] = 0xf0
    struct.pack_into("<H", boot, 22, FAT_SECTORS)
    struct.pack_into("<HH", boot, 24, 18, 2)
    boot[36] = 0
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0xA05_2026)
    boot[43:54] = b"AOS DATA   "
    boot[54:62] = b"FAT12   "
    boot[510:512] = b"\x55\xaa"

    for copy in range(2):
        fat = (1 + copy * FAT_SECTORS) * SECTOR
        image[fat:fat + 3] = b"\xf0\xff\xff"
        image[fat + 3:fat + 5] = b"\xff\x0f"  # cluster 2: end of chain

    root = (1 + 2 * FAT_SECTORS) * SECTOR
    image[root:root + 11] = b"README  TXT"
    image[root + 11] = 0x20
    struct.pack_into("<H", image, root + 26, 2)
    struct.pack_into("<I", image, root + 28, len(README))
    image[DATA_SECTOR * SECTOR:DATA_SECTOR * SECTOR + len(README)] = README
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(image)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=OUT / "seed.img")
    args = parser.parse_args()
    print(f"[aos] FAT12 data volume: {build_seed(args.out)}")


if __name__ == "__main__":
    main()
