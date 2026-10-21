"""Boot AOS in headless QEMU and verify the post-UEFI kernel handoff."""

import os
import shutil
import struct
import sys
import threading
import time
from pathlib import Path

from run import OUT, ROOT, find_qemu, media_args, prepare
from apps import MAGIC, install_binary, install_demo_folder, install_hello
from install import DATA_PARTITION_START, build_boot_image
from store import serve as serve_store

from qemu import Machine, free_port


def main():
    code, vars_image = prepare()
    data_image = OUT / "test-data.img"
    shutil.copy2(OUT / "seed.img", data_image)
    install_hello(data_image)
    fault_app = MAGIC + struct.pack("<II", 2, 0) + b"\x0f\x0b"
    install_binary(data_image, b"FAULT64 AIS", fault_app)
    spin_app = MAGIC + struct.pack("<II", 2, 0) + b"\xeb\xfe"
    install_binary(data_image, b"SPIN64  AIS", spin_app)
    install_demo_folder(data_image)
    with data_image.open("rb") as source:
        boot = source.read(512)
        root_lba = struct.unpack_from("<H", boot, 14)[0] + \
            boot[16] * struct.unpack_from("<H", boot, 22)[0]
        source.seek(root_lba * 512)
        root = source.read(struct.unpack_from("<H", boot, 17)[0] * 32)
        folder_slot = root.find(b"DOCS       ")
        assert folder_slot >= 0
        folder_index = folder_slot // 32
    boot_image = build_boot_image(OUT / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI",
                                  OUT / "test-boot.img", data_image)
    serial = OUT / "test-serial.log"
    serial.unlink(missing_ok=True)
    os.environ["QEMU"] = find_qemu()
    store_port = free_port()
    extra = media_args(code, vars_image, boot_image=boot_image, integrated=True) + [
        "-serial", f"file:{serial.as_posix()}",
        "-serial", f"tcp:127.0.0.1:{store_port},server=on,wait=off",
    ]
    with Machine(None, mem=256, cpu="qemu64", boot="c", snapshot=False, extra=extra) as m:
        store_thread = threading.Thread(target=serve_store, args=(store_port,), daemon=True)
        store_thread.start()
        deadline = time.time() + 30
        while time.time() < deadline:
            if serial.exists() and b"Framebuffer painted by kernel" in serial.read_bytes():
                break
            time.sleep(.2)
        else:
            raise AssertionError("AOS did not reach its post-UEFI kernel")
        assert b"Own page tables at 0x" in serial.read_bytes(), \
            "kernel did not install its page tables"
        assert b"AIS64 separate page tables at 0x" in serial.read_bytes(), \
            "AIS64 user runtime did not install a separate address space"
        assert b"PIT timer ticks confirmed" in serial.read_bytes(), \
            "kernel timer did not fire"
        assert b"AOS FAT12 volume found on IDE" in serial.read_bytes(), \
            "kernel did not read the existing AOS filesystem"
        assert b"PS/2 mouse enabled" in serial.read_bytes(), \
            "kernel did not enable mouse input"
        m.screenshot(str(OUT / "files-preview.png"))
        for _ in range(folder_index):
            m.key("down")
        m.key("ret")
        time.sleep(.3)
        assert b"AOS folder opened" in serial.read_bytes(), \
            "File Manager did not enter a FAT12 subdirectory"
        m.screenshot(str(OUT / "folder-preview.png"))
        m.key("ret")
        time.sleep(.3)
        assert b"AOS text file opened: GUIDE   TXT" in serial.read_bytes(), \
            "File Manager did not read text inside a folder"
        m.key("esc")
        m.key("esc")
        # Cursor starts at screen centre. Move into the Notes taskbar button.
        for axis, distance in (("x", 260 + 56 + 28 - 640), ("y", 800 - 28 - 400)):
            while distance:
                value = max(-100, min(100, distance))
                m.qmp("input-send-event", events=[{"type": "rel", "data": {
                    "axis": axis, "value": value}}])
                distance -= value
                time.sleep(.04)
        m.qmp("input-send-event", events=[{"type": "btn", "data": {
            "button": "left", "down": True}}])
        m.qmp("input-send-event", events=[{"type": "btn", "data": {
            "button": "left", "down": False}}])
        time.sleep(.6)
        assert b"Mouse taskbar application: 0000000000000001" in serial.read_bytes(), \
            "mouse click did not open the Notes taskbar button"
        m.key("esc")
        m.key("ret")
        time.sleep(.3)
        assert b"AOS text file opened" in serial.read_bytes(), \
            "kernel did not open README.TXT from FAT12"
        m.screenshot(str(OUT / "text-preview.png"))
        m.key("esc")
        m.key("right")
        m.key("ret")
        time.sleep(.3)
        assert b"Taskbar application selected: 0000000000000001" in serial.read_bytes(), \
            "kernel keyboard input did not select the Notes tile"
        note_text = "aos note" + "x" * 510 + "END"
        m.type("aos note")
        for _ in range(510):
            m.key("x", hold=1)
        m.type("END")
        time.sleep(2)
        m.key("ctrl", "s")
        time.sleep(2)
        assert b"AOS Notes saved to FAT12" in serial.read_bytes(), \
            "kernel did not save Notes"
        m.key("right")
        m.key("right")
        m.key("ret")
        m.type("dir")
        m.key("ret")
        time.sleep(.3)
        assert b"AOS terminal command: dir" in serial.read_bytes(), \
            "kernel terminal did not accept a command"
        m.key("right")
        m.key("right")
        m.key("ret")
        m.key("ret")
        time.sleep(.3)
        assert b"AIS64 program executed: Hello from AOS!" in serial.read_bytes(), \
            "native 64-bit AIS demo did not run"
        m.key("right")
        m.key("right")
        m.key("ret")
        m.type("abc")
        m.key("left")
        m.type("X")
        m.key("ctrl", "s")
        time.sleep(.4)
        assert b"AOS Write saved DOCUMENT.TXT" in serial.read_bytes(), \
            "AOS Write did not save its document"
        m.screenshot(str(OUT / "write-preview.png"))
        m.key("esc")
        for _ in range(4):
            m.key("right")
        m.key("ret")
        time.sleep(1)
        m.key("ret")
        time.sleep(3)
        assert b"AOS Store catalogue received" in serial.read_bytes(), \
            "native Store did not fetch its catalogue over COM2"
        m.key("ret")
        time.sleep(.4)
        assert b"AOS Store installed native AIS64 app" in serial.read_bytes(), \
            "native Store did not install an app"
        m.key("right")
        m.key("ret")
        m.key("down")
        m.key("down")
        m.key("down")
        m.key("ret")
        time.sleep(.3)
        assert b"AIS64 program executed: Welcome from the AOS Store!" in serial.read_bytes(), \
            "Store-installed app did not execute in ring 3"
        time.sleep(1)
        m.key("up")
        time.sleep(.3)
        m.key("ret")
        deadline = time.time() + 15
        while (b"AIS64 program load failed or faulted" not in serial.read_bytes()
               and time.time() < deadline):
            time.sleep(.2)
        assert b"AIS64 launching: SPIN64  AIS" in serial.read_bytes(), \
            "test did not launch the spinning app"
        assert b"AIS64 program load failed or faulted" in serial.read_bytes(), \
            "ring-3 time limit did not return to desktop"
        m.key("up")
        m.key("ret")
        time.sleep(.3)
        assert serial.read_bytes().count(b"AIS64 program load failed or faulted") >= 2, \
            "ring-3 invalid-instruction fault did not return to desktop"
        m.key("up")
        m.key("ret")
        time.sleep(.3)
        assert serial.read_bytes().count(b"AIS64 program executed: Hello from AOS!") >= 2, \
            "kernel did not remain usable after app fault"
        m.screenshot(str(OUT / "terminal-preview.png"))
        screenshot = OUT / "kernel-preview.png"
        m.screenshot(str(screenshot))
        with screenshot.open("rb") as image:
            header = image.read(24)
        assert header[:8] == b"\x89PNG\r\n\x1a\n"
        width, height = struct.unpack(">II", header[16:24])
        assert width >= 640 and height >= 480
    with boot_image.open("rb") as volume:
        volume.seek(DATA_PARTITION_START * 512)
        boot = volume.read(512)
        reserved = struct.unpack_from("<H", boot, 14)[0]
        root_lba = DATA_PARTITION_START + reserved + \
            boot[16] * struct.unpack_from("<H", boot, 22)[0]
        root_size = struct.unpack_from("<H", boot, 17)[0] * 32
        volume.seek(root_lba * 512)
        root = volume.read(root_size)
        at = root.find(b"NOTES   TXT")
        assert at >= 0, "Notes directory entry missing after save"
        cluster = struct.unpack_from("<H", root, at + 26)[0]
        size = struct.unpack_from("<I", root, at + 28)[0]
        data_lba = root_lba + (root_size + 511) // 512
        volume.seek((data_lba + cluster - 2) * 512)
        assert size == len(note_text), "saved Notes size is incorrect"
        first = volume.read(512)
        fat_size = struct.unpack_from("<H", boot, 22)[0]
        volume.seek((DATA_PARTITION_START + reserved) * 512)
        fat = volume.read(fat_size * 512)
        offset = cluster * 3 // 2
        pair = int.from_bytes(fat[offset:offset + 2], "little")
        next_cluster = pair >> 4 if cluster & 1 else pair & 0xfff
        assert 2 <= next_cluster < 0xff8, "Notes has no second FAT12 cluster"
        volume.seek((data_lba + next_cluster - 2) * 512)
        assert (first + volume.read(size - 512)) == note_text.encode(), \
            "multi-cluster Notes content missing"
        document_at = root.find(b"DOCUMENTTXT")
        assert document_at >= 0, "AOS Write document missing"
        document_cluster = struct.unpack_from("<H", root, document_at + 26)[0]
        document_size = struct.unpack_from("<I", root, document_at + 28)[0]
        volume.seek((data_lba + document_cluster - 2) * 512)
        assert volume.read(document_size) == b"abXc", \
            "AOS Write insertion or save failed"
        assert b"WELCOME AIS" in root, "Store-installed app missing from FAT12 root"
    with boot_image.open("rb") as installed:
        installed.seek(DATA_PARTITION_START * 512)
        saved_volume = installed.read(data_image.stat().st_size)
    build_boot_image(OUT / "esp" / "EFI" / "BOOT" / "BOOTX64.EFI",
                     boot_image, data_image, preserve_data=True)
    with boot_image.open("rb") as installed:
        installed.seek(DATA_PARTITION_START * 512)
        assert installed.read(len(saved_volume)) == saved_volume, \
            "reinstalling the EFI bootloader erased AOS files"
    print(f"[PASS] AOS UEFI, graphics, mouse, FAT12, Write, Store and protected AIS64 apps at {width}x{height}")


if __name__ == "__main__":
    main()
