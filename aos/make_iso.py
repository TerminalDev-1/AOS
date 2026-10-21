"""Build a UEFI El Torito ISO for the self-contained AOS live desktop."""

import argparse
import struct
from pathlib import Path

from install import PARTITION_START, SECTOR, build_boot_image

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "out"
ISO_VOLUME_SECTORS = 16384
ISO_FAT_SECTORS = 32


def build_iso(efi_path: Path, seed_path: Path, iso_path: Path):
    try:
        import pycdlib
    except ImportError as error:
        raise SystemExit("ISO creation needs pycdlib: python -m pip install pycdlib") from error
    OUT.mkdir(parents=True, exist_ok=True)
    raw_disk = OUT / "iso-efi-disk.img"
    efi_volume = OUT / "iso-efi-volume.img"
    build_boot_image(efi_path, raw_disk,
                     volume_sectors=ISO_VOLUME_SECTORS,
                     fat_sectors=ISO_FAT_SECTORS)
    with raw_disk.open("rb") as source, efi_volume.open("wb") as destination:
        source.seek(PARTITION_START * SECTOR)
        remaining = ISO_VOLUME_SECTORS * SECTOR
        while remaining:
            block = source.read(min(1024 * 1024, remaining))
            if not block:
                raise ValueError("EFI volume image was truncated")
            destination.write(block)
            remaining -= len(block)
    with efi_volume.open("r+b") as volume:
        boot = bytearray(volume.read(SECTOR))
        struct.pack_into("<I", boot, 28, 0)  # no MBR offset inside El Torito media
        volume.seek(0)
        volume.write(boot)
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3, vol_ident="AOS_LIVE")
    iso.add_file(str(efi_volume), iso_path="/EFIBOOT.IMG;1")
    iso.add_file(str(seed_path), iso_path="/AOSDATA.IMG;1")
    iso.add_directory("/EFI")
    iso.add_directory("/EFI/BOOT")
    iso.add_file(str(efi_path), iso_path="/EFI/BOOT/BOOTX64.EFI;1")
    iso.add_eltorito("/EFIBOOT.IMG;1", efi=True, platform_id=0xef,
                     media_name="noemul", boot_load_size=ISO_VOLUME_SECTORS)
    iso.write(str(iso_path))
    iso.close()
    return iso_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=OUT / "aos.iso")
    args = parser.parse_args()
    efi = OUT / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI"
    seed = OUT / "seed.img"
    if not efi.is_file() or not seed.is_file():
        raise SystemExit("Build AOS first with python aos/test.py or python aos/run.py")
    build_iso(efi, seed, args.out)
    print(f"[aos] bootable ISO: {args.out} ({args.out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
