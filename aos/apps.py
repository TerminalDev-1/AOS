"""Build and stage the first versioned native AOS application.

AIS64 v1 is an 8-byte magic/version, little-endian code size and entry offset,
then position-independent x86-64 code. It runs in ring 3 at 0x20000000 and
uses INT 80h: RAX=1 writes RDI/RSI bytes, RAX=0 exits.
"""

import struct
from pathlib import Path

MAGIC = b"AIS64\0\1\0"
NAME = b"HELLO   AIS"
MESSAGE = b"Hello from AOS!"


def ais64_message_binary(message: bytes):
    # lea rdi,[rip+16]; mov esi,message length; mov eax,1; int 80h;
    # xor eax,eax; int 80h; followed by MESSAGE.
    code = (bytes.fromhex("48 8d 3d 10 00 00 00 be")
            + struct.pack("<I", len(message))
            + bytes.fromhex("b8 01 00 00 00 cd 80 31 c0 cd 80")
            + message)
    return MAGIC + struct.pack("<II", len(code), 0) + code


def hello_binary():
    return ais64_message_binary(MESSAGE)


def _set_fat12(image, fat_start, cluster, value):
    offset = fat_start + cluster * 3 // 2
    pair = int.from_bytes(image[offset:offset + 2], "little")
    pair = ((pair & 0x000f) | (value << 4)) if cluster & 1 else \
           ((pair & 0xf000) | value)
    image[offset:offset + 2] = pair.to_bytes(2, "little")


def install_binary(image_path: Path, name: bytes, app: bytes,
                   output_path: Path | None = None):
    if len(name) != 11 or len(app) < 17 or app[:8] != MAGIC:
        raise ValueError("invalid AIS64 application")
    if output_path is not None:
        output_path.write_bytes(app)
    image = bytearray(image_path.read_bytes())
    sector_size = struct.unpack_from("<H", image, 11)[0]
    sectors_per_cluster = image[13]
    reserved = struct.unpack_from("<H", image, 14)[0]
    fat_count = image[16]
    root_entries = struct.unpack_from("<H", image, 17)[0]
    fat_sectors = struct.unpack_from("<H", image, 22)[0]
    if sector_size != 512 or sectors_per_cluster != 1 or len(app) > 512:
        raise ValueError("unsupported AOS demo disk layout")
    root_sector = reserved + fat_count * fat_sectors
    root_start = root_sector * 512
    root_end = root_start + root_entries * 32
    data_sector = root_sector + (root_entries * 32 + 511) // 512
    slot = None
    for at in range(root_start, root_end, 32):
        if image[at:at + 11] == name:
            cluster = struct.unpack_from("<H", image, at + 26)[0]
            if cluster < 2:
                raise ValueError("invalid existing AIS64 cluster")
            data_at = (data_sector + cluster - 2) * 512
            if image[data_at:data_at + len(app)] != app:
                image[data_at:data_at + 512] = app.ljust(512, b"\0")
                struct.pack_into("<I", image, at + 28, len(app))
                image_path.write_bytes(image)
            return
        if slot is None and image[at] in (0, 0xe5):
            slot = at
    if slot is None:
        raise ValueError("FAT12 root directory is full")
    fat_start = reserved * 512
    cluster = None
    for candidate in range(2, len(image) // 512 - data_sector + 2):
        offset = fat_start + candidate * 3 // 2
        pair = int.from_bytes(image[offset:offset + 2], "little")
        value = pair >> 4 if candidate & 1 else pair & 0xfff
        if value == 0:
            cluster = candidate
            break
    if cluster is None:
        raise ValueError("FAT12 volume is full")
    for copy in range(fat_count):
        _set_fat12(image, (reserved + copy * fat_sectors) * 512,
                   cluster, 0xfff)
    data_at = (data_sector + cluster - 2) * 512
    image[data_at:data_at + len(app)] = app
    image[slot:slot + 11] = name
    image[slot + 11] = 0x20
    struct.pack_into("<H", image, slot + 26, cluster)
    struct.pack_into("<I", image, slot + 28, len(app))
    image_path.write_bytes(image)


def install_hello(image_path: Path, output_path: Path | None = None):
    install_binary(image_path, NAME, hello_binary(), output_path)


def install_demo_folder(image_path: Path, volume_offset=0):
    """Add a small FAT12 subdirectory for File Manager navigation tests."""
    image = bytearray(image_path.read_bytes())
    reserved = struct.unpack_from("<H", image, volume_offset + 14)[0]
    fat_count = image[volume_offset + 16]
    fat_sectors = struct.unpack_from("<H", image, volume_offset + 22)[0]
    root_entries = struct.unpack_from("<H", image, volume_offset + 17)[0]
    total_sectors = struct.unpack_from("<H", image, volume_offset + 19)[0]
    if not total_sectors:
        total_sectors = struct.unpack_from("<I", image, volume_offset + 32)[0]
    root_start = volume_offset + (reserved + fat_count * fat_sectors) * 512
    data_sector = reserved + fat_count * fat_sectors + (root_entries * 32 + 511) // 512
    slot = None
    for at in range(root_start, root_start + root_entries * 32, 32):
        if image[at:at + 11] == b"DOCS       ":
            return
        if slot is None and image[at] in (0, 0xe5):
            slot = at
    if slot is None:
        raise ValueError("no FAT12 root entry for DOCS")
    free = []
    fat_start = volume_offset + reserved * 512
    for candidate in range(2, total_sectors - data_sector + 2):
        offset = fat_start + candidate * 3 // 2
        pair = int.from_bytes(image[offset:offset + 2], "little")
        value = pair >> 4 if candidate & 1 else pair & 0xfff
        if value == 0:
            free.append(candidate)
            if len(free) == 2:
                break
    if len(free) != 2:
        raise ValueError("no free clusters for DOCS")
    folder, guide = free
    for copy in range(fat_count):
        start = volume_offset + (reserved + copy * fat_sectors) * 512
        _set_fat12(image, start, folder, 0xfff)
        _set_fat12(image, start, guide, 0xfff)

    def entry(name, attr, cluster, size=0):
        record = bytearray(32)
        record[:11] = name
        record[11] = attr
        struct.pack_into("<H", record, 26, cluster)
        struct.pack_into("<I", record, 28, size)
        return record

    content = b"AOS can browse FAT12 folders.\r\n"
    image[slot:slot + 32] = entry(b"DOCS       ", 0x10, folder)
    at = volume_offset + (data_sector + folder - 2) * 512
    image[at:at + 32] = entry(b".          ", 0x10, folder)
    image[at + 32:at + 64] = entry(b"..         ", 0x10, 0)
    image[at + 64:at + 96] = entry(b"GUIDE   TXT", 0x20, guide, len(content))
    at = volume_offset + (data_sector + guide - 2) * 512
    image[at:at + len(content)] = content
    image_path.write_bytes(image)
