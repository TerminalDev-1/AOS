"""Create a standalone UEFI USB disk image containing AOS's bootloader."""

import argparse
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "out"
SECTOR = 512
PARTITION_START = 2048
VOLUME_SECTORS = 65536
FAT_SECTORS = 128
ROOT_ENTRIES = 512
CLUSTER_SECTORS = 2
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR
DATA_LBA = 1 + 2 * FAT_SECTORS + ROOT_SECTORS
DATA_PARTITION_START = PARTITION_START + VOLUME_SECTORS


def _entry(name, attributes, cluster, size=0):
    result = bytearray(32)
    result[:11] = name
    result[11] = attributes
    struct.pack_into("<H", result, 26, cluster)
    struct.pack_into("<I", result, 28, size)
    return result


def build_boot_image(efi_path: Path, image_path: Path,
                     data_path: Path | None = None, preserve_data=False,
                     volume_sectors=VOLUME_SECTORS, fat_sectors=FAT_SECTORS):
    binary = efi_path.read_bytes()
    data_lba = 1 + 2 * fat_sectors + ROOT_SECTORS
    data_partition_start = PARTITION_START + volume_sectors
    clusters = max(1, math.ceil(len(binary) / (SECTOR * CLUSTER_SECTORS)))
    if clusters + 4 > (volume_sectors - data_lba) // CLUSTER_SECTORS:
        raise ValueError("EFI binary does not fit the boot volume")
    data = data_path.read_bytes() if data_path is not None else b""
    if data and len(data) % SECTOR != 0:
        raise ValueError("data volume must have whole sectors")
    if data and preserve_data and image_path.exists():
        previous = image_path.read_bytes()
        at = data_partition_start * SECTOR
        if len(previous) >= at + len(data) and previous[462 + 4] == 0x01:
            data = previous[at:at + len(data)]
    image = bytearray((data_partition_start + len(data) // SECTOR) * SECTOR)
    partition = bytearray(16)
    partition[4] = 0xef
    struct.pack_into("<II", partition, 8, PARTITION_START, volume_sectors)
    image[446:462] = partition
    if data:
        data_partition = bytearray(16)
        data_partition[4] = 0x01
        struct.pack_into("<II", data_partition, 8,
                         data_partition_start, len(data) // SECTOR)
        image[462:478] = data_partition
        at = data_partition_start * SECTOR
        image[at:at + len(data)] = data
    image[510:512] = b"\x55\xaa"

    volume = PARTITION_START * SECTOR
    boot = bytearray(SECTOR)
    boot[:3] = b"\xeb\x3c\x90"
    boot[3:11] = b"AOS     "
    struct.pack_into("<H", boot, 11, SECTOR)
    boot[13] = CLUSTER_SECTORS
    struct.pack_into("<H", boot, 14, 1)
    boot[16] = 2
    struct.pack_into("<H", boot, 17, ROOT_ENTRIES)
    if volume_sectors <= 0xffff:
        struct.pack_into("<H", boot, 19, volume_sectors)
    boot[21] = 0xf8
    struct.pack_into("<H", boot, 22, fat_sectors)
    struct.pack_into("<HH", boot, 24, 63, 255)
    struct.pack_into("<I", boot, 28, PARTITION_START)
    if volume_sectors > 0xffff:
        struct.pack_into("<I", boot, 32, volume_sectors)
    boot[36] = 0x80
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0xA064_2026)
    boot[43:54] = b"AOS EFI    "
    boot[54:62] = b"FAT16   "
    boot[510:512] = b"\x55\xaa"
    image[volume:volume + SECTOR] = boot

    fat = bytearray(fat_sectors * SECTOR)
    struct.pack_into("<HHHH", fat, 0, 0xfff8, 0xffff, 0xffff, 0xffff)
    for index in range(clusters):
        current = 4 + index
        following = current + 1 if index + 1 < clusters else 0xffff
        struct.pack_into("<H", fat, current * 2, following)
    for copy in range(2):
        at = volume + (1 + copy * fat_sectors) * SECTOR
        image[at:at + len(fat)] = fat

    root = volume + (1 + 2 * fat_sectors) * SECTOR
    image[root:root + 32] = _entry(b"EFI        ", 0x10, 2)

    def cluster_at(number):
        return volume + (data_lba + (number - 2) * CLUSTER_SECTORS) * SECTOR

    efi_dir = cluster_at(2)
    image[efi_dir:efi_dir + 32] = _entry(b".          ", 0x10, 2)
    image[efi_dir + 32:efi_dir + 64] = _entry(b"..         ", 0x10, 0)
    image[efi_dir + 64:efi_dir + 96] = _entry(b"BOOT       ", 0x10, 3)
    boot_dir = cluster_at(3)
    image[boot_dir:boot_dir + 32] = _entry(b".          ", 0x10, 3)
    image[boot_dir + 32:boot_dir + 64] = _entry(b"..         ", 0x10, 2)
    image[boot_dir + 64:boot_dir + 96] = _entry(
        b"BOOTX64 EFI", 0x20, 4, len(binary))
    for index in range(clusters):
        chunk = binary[index * 1024:(index + 1) * 1024]
        at = cluster_at(4 + index)
        image[at:at + len(chunk)] = chunk
    image_path.parent.mkdir(parents=True, exist_ok=True)
    image_path.write_bytes(image)
    return image_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=OUT / "aos-boot.img")
    parser.add_argument("--data", type=Path, default=OUT / "data.img")
    args = parser.parse_args()
    efi = OUT / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI"
    if not efi.is_file():
        raise SystemExit("Build AOS first with python aos/run.py or python aos/test.py")
    if not args.data.is_file():
        raise SystemExit("AOS data image missing; run python aos/run.py first")
    build_boot_image(efi, args.out, args.data, preserve_data=True)
    print(f"[aos] UEFI boot image: {args.out} ({args.out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
