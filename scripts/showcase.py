"""Walk through every feature of WinKit and save labelled screenshots.

Writes out/screens/showcase/NN-name.png plus overview.png (a contact sheet).
Usage: python scripts/showcase.py
"""
import struct
import time
from pathlib import Path
from qemu import Machine

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "out" / "screens" / "showcase"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.png"):
        old.unlink()
    shots = []

    with Machine(str(ROOT / "out" / "winkit.img")) as m:
        deadline = time.time() + 30
        while m.video_mode() != 0x12:
            if time.time() > deadline:
                raise AssertionError("desktop did not start")
            time.sleep(.2)
        time.sleep(1)
        memory = Path(m.tmp) / "ram.bin"
        m.qmp("pmemsave", val=0, size=640 * 1024, filename=str(memory))
        addr = memory.read_bytes().rfind(b"V4GUIstate") + 10

        def state():
            m.qmp("pmemsave", val=addr, size=9, filename=str(memory))
            return struct.unpack("<5B2H", memory.read_bytes())

        def mouse(x, y, down=None):
            # Absolute, as a host pointer over the emulator window is: each
            # axis runs 0..0x7fff across the screen (src/VMMOUSE.INC).
            m.qmp("input-send-event", events=[
                {"type": "abs", "data": {
                    "axis": "x", "value": -(-x * 0x8000 // 848)}},
                {"type": "abs", "data": {
                    "axis": "y", "value": -(-y * 0x8000 // 480)}}])
            time.sleep(.25)
            if down is not None:
                m.qmp("input-send-event", events=[
                    {"type": "btn", "data": {"button": "left", "down": down}}])
                time.sleep(.25)

        def click(x, y):
            mouse(x, y, True)
            mouse(x, y, False)
            time.sleep(.6)

        def shot(name, caption):
            time.sleep(1)
            m.qmp("stop")
            path = OUT / f"{len(shots) + 1:02d}-{name}.png"
            m.screenshot(str(path))
            m.qmp("cont")
            shots.append((path, caption))
            print("saved", path.name, flush=True)

        def run(command):
            m.type(command)
            m.key("ret")
            time.sleep(.7)

        shot("welcome", "Boots straight to the desktop (no DOS)")
        click(434, 320)                  # the dialog is centred on 848
        shot("desktop", "Desktop: icons, taskbar, tray clock")
        click(44, 40)
        shot("icon-selected", "Single click selects a desktop icon")

        click(30, 466)
        mouse(100, 286)
        shot("start-menu", "Start menu with hover highlight")
        click(100, 286)
        m.type("Hello from WinKit")
        m.key("f4")
        time.sleep(1.5)
        shot("notes", "Notes: typing, Save (F4) to the FAT12 disk")

        m.key("f1")
        shot("files", "Files: root of the WinKit volume")
        m.key("f3")
        shot("system", "System Properties window")

        mouse(300, 92, True)
        mouse(380, 150)
        mouse(380, 150, False)
        shot("dragged", "Dragging a window (latch blit, no flicker)")
        click(360, 466)
        shot("minimized", "Minimized to the taskbar")
        click(360, 466)

        m.key("f6")
        run("dir")
        shot("terminal-dir", "WinKit terminal: dir")
        run("type notes.txt")
        run("hello")
        shot("terminal-program", "Reads a file, runs a native .WKX program")
        run("cls")
        run("count")
        run("echo saved by winkit > wk.txt")
        run("type wk.txt")
        shot("terminal-files", "count program, echo > file, type")
        run("help")
        shot("terminal-help", "help: built-in commands")

        m.key("f10")
        shot("shutdown", "Shut Down: Shut down / Restart")

    contact_sheet(shots)


def contact_sheet(shots):
    from PIL import Image, ImageDraw
    cols, cw, ch, pad, cap = 3, 400, 300, 14, 22
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (cw + pad) + pad,
                              rows * (ch + cap + pad) + pad), (32, 32, 40))
    draw = ImageDraw.Draw(sheet)
    for i, (path, caption) in enumerate(shots):
        x = pad + (i % cols) * (cw + pad)
        y = pad + (i // cols) * (ch + cap + pad)
        img = Image.open(path).convert("RGB").resize((cw, ch), Image.LANCZOS)
        sheet.paste(img, (x, y + cap))
        draw.text((x, y + 4), f"{i + 1}. {caption}", fill=(235, 235, 235))
    target = OUT / "overview.png"
    sheet.save(target)
    print("saved", target.name)


if __name__ == "__main__":
    main()
