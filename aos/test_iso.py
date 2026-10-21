"""Boot the AOS El Torito ISO without an ATA data disk."""

import hashlib
import os
import sys
import time
from pathlib import Path

from PIL import Image, ImageChops

from make_iso import build_iso
from run import OUT, ESP, find_qemu, prepare

from qemu import Machine


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.digest()


def main():
    code, vars_image = prepare()
    iso = build_iso(ESP / "EFI" / "BOOT" / "BOOTX64.EFI",
                    OUT / "seed.img", OUT / "test-aos.iso")
    before = digest(iso)
    serial = OUT / "iso-serial.log"
    serial.unlink(missing_ok=True)
    os.environ["QEMU"] = find_qemu()
    extra = [
        "-drive", f"if=pflash,unit=0,format=raw,readonly=on,file={code.as_posix()}",
        "-drive", f"if=pflash,unit=1,format=raw,file={vars_image.as_posix()}",
        "-cdrom", str(iso),
        "-serial", f"file:{serial.as_posix()}",
    ]
    with Machine(None, mem=256, cpu="qemu64", boot="d",
                 snapshot=False, extra=extra) as machine:
        deadline = time.time() + 45
        boot_logo_seen = False
        while time.time() < deadline:
            if serial.exists() and b"Framebuffer painted by kernel" in serial.read_bytes():
                break
            machine.screenshot(str(OUT / "boot-candidate.png"))
            with Image.open(OUT / "boot-candidate.png") as candidate:
                w, h = candidate.size
                r, g, b = candidate.convert("RGB").split()
                red = ImageChops.multiply(r.point(lambda v: 255 if v > 150 else 0),
                    ImageChops.multiply(g.point(lambda v: 255 if v < 100 else 0),
                                        b.point(lambda v: 255 if v < 110 else 0)))
                bounds = red.getbbox()
                if bounds and bounds[2] - bounds[0] > w * .65:
                    candidate.save(OUT / "boot-preview.png")
                    boot_logo_seen = True
            time.sleep(.2)
        else:
            raise AssertionError("AOS ISO did not boot through UEFI")
        assert boot_logo_seen, "boot screen did not display the red AOS logo"
        assert b"AOS using writable RAM disk from boot media" in serial.read_bytes(), \
            "ISO boot did not select RAM-backed data"
        assert b"AOS FAT12 volume found in RAM" in serial.read_bytes(), \
            "ISO boot did not mount its data image"
        machine.screenshot(str(OUT / "iso-preview.png"))
        time.sleep(2)
        machine.screenshot(str(OUT / "iso-preview-stable.png"))
        with Image.open(OUT / "iso-preview.png") as first, \
             Image.open(OUT / "iso-preview-stable.png") as stable:
            width, height = first.size
            taskbar_box = (0, height - 56, width - 180, height)
            idle_taskbar = first.crop(taskbar_box).tobytes()
            assert idle_taskbar == stable.crop(taskbar_box).tobytes(), \
                "idle taskbar changed between frames"
        machine.qmp("input-send-event", events=[{"type": "rel", "data": {
            "axis": "x", "value": 80}}])
        time.sleep(.2)
        machine.screenshot(str(OUT / "iso-cursor-moved.png"))
        with Image.open(OUT / "iso-cursor-moved.png") as moved:
            background = moved.getpixel((width // 2 - 20, height // 2 + 40))
            old_cursor = moved.crop((width // 2, height // 2,
                                     width // 2 + 24, height // 2 + 30))
            assert old_cursor.tobytes() == bytes(background) * (24 * 30), \
                "moving the mouse left ghost pixels behind"
        machine.key("right")
        time.sleep(.8)
        machine.screenshot(str(OUT / "iso-taskbar-selected.png"))
        time.sleep(.5)
        machine.screenshot(str(OUT / "iso-taskbar-selected-stable.png"))
        with Image.open(OUT / "iso-taskbar-selected.png") as selected, \
             Image.open(OUT / "iso-taskbar-selected-stable.png") as settled:
            changed_taskbar = selected.crop(taskbar_box).tobytes()
            assert changed_taskbar != idle_taskbar, "taskbar did not show the selected app"
            assert changed_taskbar == settled.crop(taskbar_box).tobytes(), \
                "taskbar kept changing after selecting an app"
        machine.key("ret")
        time.sleep(.4)
        machine.screenshot(str(OUT / "iso-notes-preview.png"))
        with Image.open(OUT / "iso-notes-preview.png") as notes:
            assert notes.crop(taskbar_box).tobytes() != changed_taskbar, \
                "taskbar did not update its running application indicator"
        machine.type("iso note")
        machine.key("ctrl", "s")
        time.sleep(.5)
        assert b"AOS Notes saved to FAT12" in serial.read_bytes(), \
            "ISO live session could not write to RAM disk"
        cursor_x, cursor_y = width // 2 + 80, height // 2

        def click(x, y):
            nonlocal cursor_x, cursor_y
            for axis, target in (("x", x), ("y", y)):
                current = cursor_x if axis == "x" else cursor_y
                while current != target:
                    step = max(-80, min(80, target - current))
                    machine.qmp("input-send-event", events=[{"type": "rel", "data": {
                        "axis": axis, "value": step}}])
                    current += step
                    time.sleep(.04)
                if axis == "x":
                    cursor_x = current
                else:
                    cursor_y = current
            for down in (True, False):
                machine.qmp("input-send-event", events=[{"type": "btn", "data": {
                    "button": "left", "down": down}}])
                time.sleep(.1)
            time.sleep(.2)

        normal_width = min(width * 3 // 5, 780)
        normal_height = min(height * 3 // 5, 480)
        normal_x = (width - normal_width) // 2
        normal_y = (height - normal_height) // 2 - 12
        click(normal_x + normal_width - 77, normal_y + 24)
        assert b"AOS window maximized" in serial.read_bytes(), "maximize button did not work"
        machine.screenshot(str(OUT / "window-maximized-preview.png"))
        click(width - 77, 68)
        assert b"AOS window restored" in serial.read_bytes(), "restore button did not work"
        click(normal_x + normal_width - 123, normal_y + 24)
        assert b"AOS window minimized" in serial.read_bytes(), "minimize button did not work"
        machine.screenshot(str(OUT / "window-minimized-preview.png"))
        machine.key("ret")
        machine.key("ctrl", "s")
        assert serial.read_bytes().count(b"AOS Notes saved to FAT12") >= 2, \
            "minimized Notes did not reopen with a working editor"
        click(normal_x + normal_width - 31, normal_y + 24)
        assert b"AOS window closed" in serial.read_bytes(), "close button did not work"
        machine.key("ret")
        deadline = time.time() + 3
        while (serial.read_bytes().count(b"AOS window reopened") < 2
               and time.time() < deadline):
            time.sleep(.1)
        assert serial.read_bytes().count(b"AOS window reopened") >= 2, \
            "closed app could not reopen from its taskbar selection"
        machine.screenshot(str(OUT / "window-controls-preview.png"))

        def wait_log(message, count=1):
            deadline = time.time() + 5
            while serial.read_bytes().count(message) < count and time.time() < deadline:
                time.sleep(.1)
            assert serial.read_bytes().count(message) >= count, message.decode()

        machine.key("meta_l")
        wait_log(b"AOS Start menu opened")
        machine.screenshot(str(OUT / "start-menu-preview.png"))
        machine.type("write")
        time.sleep(.4)
        machine.screenshot(str(OUT / "start-search-preview.png"))
        for _ in range(5):
            machine.key("backspace")
        time.sleep(.3)
        machine.screenshot(str(OUT / "start-restored-preview.png"))
        with Image.open(OUT / "start-menu-preview.png") as original, \
             Image.open(OUT / "start-restored-preview.png") as restored:
            menu_box = (0, height - 624, 556, height - 56)
            assert original.crop(menu_box).tobytes() == restored.crop(menu_box).tobytes(), \
                "Start menu repaint accumulated shadows or left stale query pixels"
        machine.type("write")
        machine.key("ret")
        wait_log(b"AOS Start launched: Write")
        machine.type("aos write")
        machine.key("ctrl", "s")
        wait_log(b"AOS Write saved DOCUMENT.TXT")
        machine.screenshot(str(OUT / "aos-desktop-preview.png"))
        click(28, height - 28)
        wait_log(b"AOS Start menu opened", 2)
        menu_y = height - 56 - min(560, height - 72)
        click(470, menu_y + 198)
        wait_log(b"AOS Start launched: Notes")
        click(28, height - 28)
        wait_log(b"AOS Start menu opened", 3)
        click(455, height - 85)
        wait_log(b"AOS Start launched: Power")
        machine.key("esc")
        click(28, height - 28)
        wait_log(b"AOS Start menu opened", 4)
        click(width - 100, height - 150)
        wait_log(b"AOS Start menu dismissed")
        click(288, height - 28)
        wait_log(b"AOS taskbar minimized window")
        click(288, height - 28)
        wait_log(b"Mouse taskbar application: 0000000000000000", 2)
        machine.key("ctrl", "esc")
        wait_log(b"AOS Start menu opened", 5)
        machine.key("esc")
        time.sleep(.4)
        machine.screenshot(str(OUT / "start-dismissed-preview.png"))
    assert digest(iso) == before, "running the live system modified its ISO"
    print(f"[PASS] AOS ISO boot, full-screen red splash, Start search/pins/power, taskbar, windows, cursor and saves: {iso}")


if __name__ == "__main__":
    main()
