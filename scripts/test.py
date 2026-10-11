"""Exercise WinKit in QEMU: boot, desktop, filesystem, terminal, programs.

There is no DOS in the image, so everything below is the system's own code:
the desktop, the FAT12 driver and the WinKit terminal.
"""
import argparse
import struct
import threading
import time
from pathlib import Path

import wkstore
from qemu import Machine, free_port

ROOT = Path(__file__).resolve().parent.parent
# QEMU gets the wide screen (src/VIDEO.INC): 848 x 480. The two dialogs
# are centred, so they sit DLG pixels right of their 640-wide positions,
# and a program's window is at WIN_X with its client area at CX, CY.
WIDTH = 848
DLG = (WIDTH - 640) // 2
WIN_X, WIN_Y = (WIDTH - 416) // 2, 72
CX, CY = WIN_X + 3, WIN_Y + 21
SHOTS = ROOT / "out" / "screens" / "gui"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--image", default=str(ROOT / "out" / "winkit.img"),
                    help="image to test (build one with mkimage.py --out)")
    image = ap.parse_args().image
    SHOTS.mkdir(parents=True, exist_ok=True)
    checks = 0
    store_port = free_port()             # the store server, on the serial port
    serial = ("-serial", f"tcp:127.0.0.1:{store_port},server=on,wait=off")
    with Machine(image, extra=serial) as m:
        threading.Thread(target=wkstore.serve, args=(store_port,),
                         kwargs={"log": lambda text: None},
                         daemon=True).start()
        memory = Path(m.tmp) / "ram.bin"

        def wait_gui():
            m.wait_desktop()             # the boot screen comes first

        def locate():
            m.qmp("pmemsave", val=0, size=640 * 1024, filename=str(memory))
            ram = memory.read_bytes()
            state = ram.rfind(b"V4GUIstate") + 10
            term = ram.rfind(b"WKTERMBUF") + 9
            assert state > 10 and term > 9, "telemetry not found"
            return state, term

        def fm():
            """The File Manager: (selected file or 0xFFFF, what it is asking
            for: 0 nothing, 1 a new name, 2 a copy's name, 3 delete yes/no,
            free 512-byte clusters). They follow "WKFMstate" (DATA.INC)."""
            m.qmp("pmemsave", val=0x10000, size=0x10000, filename=str(memory))
            ram = memory.read_bytes()
            at = ram.find(b"WKFMstate")
            assert at > 0, "File Manager telemetry not found"
            return struct.unpack("<HBH", ram[at + 9:at + 14])

        def fm_row(n):
            """A point on row n of its list (the window has not been moved)."""
            return 96 + 100, 20 + 79 + n * 18 + 9

        def fm_button(n):
            """Its toolbar: 0 Open, 1 Copy, 2 Rename, 3 Delete."""
            return 96 + (28, 88, 152, 220)[n], 20 + 37

        def gui():
            """(1, title) while a program has its window up, else (0, ...).

            The kernel keeps that flag and the title just before the name
            of the Store program (DATA.INC)."""
            m.qmp("pmemsave", val=0x10000, size=0x10000, filename=str(memory))
            ram = memory.read_bytes()
            at = ram.rfind(b"WKSTORE.WKX\0")
            assert at > 43, "window telemetry not found"
            title = ram[at - 42:at].split(b"\0")[0].decode("latin-1")
            return ram[at - 43], title

        deadline = time.time() + 30      # the kernel draws its boot screen
        while m.video_mode() != 0x12:    # as soon as it has set the mode
            assert time.time() < deadline, "WinKit did not start"
            time.sleep(.1)
        time.sleep(.8)
        booting = m.view_state()
        m.qmp("stop")
        m.screenshot(str(SHOTS / "00-boot.png"))
        m.qmp("cont")
        wait_gui()
        state_addr, term_addr = locate()

        def state():
            m.qmp("pmemsave", val=state_addr, size=9, filename=str(memory))
            return struct.unpack("<5B2H", memory.read_bytes())

        def term_text():
            m.qmp("pmemsave", val=term_addr, size=16 * 52, filename=str(memory))
            raw = memory.read_bytes()
            rows = [raw[i * 52:(i + 1) * 52].split(b"\0")[0].decode("latin-1")
                    for i in range(16)]
            return "\n".join(rows)

        def check(name, condition):
            nonlocal checks
            assert condition, name
            checks += 1
            print(f"[PASS] {name}", flush=True)

        def shot(name):
            time.sleep(1)                # settle, then freeze for the capture
            m.qmp("stop")
            m.screenshot(str(SHOTS / (name + ".png")))
            m.qmp("cont")

        def mouse(x, y, down=None):
            # Absolute, as a host pointer over the emulator window is: each
            # axis runs 0..0x7fff across the screen (src/VMMOUSE.INC).
            m.qmp("input-send-event", events=[
                {"type": "abs", "data": {
                    "axis": "x", "value": -(-x * 0x8000 // WIDTH)}},
                {"type": "abs", "data": {
                    "axis": "y", "value": -(-y * 0x8000 // 480)}}])
            time.sleep(.2)
            if down is not None:
                m.qmp("input-send-event", events=[
                    {"type": "btn", "data": {"button": "left", "down": down}}])
                time.sleep(.2)

        def click(x, y):
            mouse(x, y, True)
            mouse(x, y, False)
            time.sleep(.5)

        def dblclick(x, y):
            mouse(x, y)
            for down in (True, False, True, False):
                m.qmp("input-send-event", events=[
                    {"type": "btn", "data": {"button": "left", "down": down}}])
                time.sleep(.15)
            time.sleep(.5)

        def run(command, wait=.6):
            m.type("cls")                # so only this command's output shows
            m.key("ret")
            time.sleep(.3)
            m.type(command)
            m.key("ret")
            time.sleep(wait)
            return term_text()

        # ---- boot and desktop ----
        m.screenshot(str(SHOTS / "00-size.png"))
        with open(SHOTS / "00-size.png", "rb") as fh:
            size = struct.unpack(">2I", fh.read(24)[16:])
        check("the QEMU display driver gives a widescreen desktop",
              size == (WIDTH, 480))
        check("a boot screen is shown while the system starts, then the "
              "welcome dialog", booting == 4 and state()[0] == 0)
        check("PS/2 mouse enabled without any driver", state()[4] == 1)
        # The QEMU mouse driver: an absolute position (0..0x7fff on each
        # axis) puts WinKit's pointer right there, like a host pointer does.
        m.qmp("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x",
                                     "value": 600 * 0x8000 // WIDTH}},
            {"type": "abs", "data": {"axis": "y",
                                     "value": 400 * 0x8000 // 480}}])
        time.sleep(.5)
        check("the QEMU mouse driver follows absolute pointer positions",
              abs(state()[5] - 600) <= 2 and abs(state()[6] - 400) <= 2)
        m.qmp("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": 0x7fff}},
            {"type": "abs", "data": {"axis": "y", "value": 0x7fff}}])
        time.sleep(.5)
        check("the pointer reaches the bottom right corner",
              state()[5] >= WIDTH - 3 and state()[6] >= 477)
        shot("01-welcome")
        click(DLG + 330, 320)                  # OK on the welcome dialog
        check("mouse enters desktop", state()[0] == 1)
        click(30, 466)                   # Start button
        click(100, 258)                  # File Manager in the Start menu
        check("the File Manager opens from the Start menu", state()[1] == 1)
        m.key("pgdn")
        shot("02-files-page-two")
        m.key("pgup")
        m.key("f2")
        m.type("Assembly GUI notes")
        m.key("f4")
        time.sleep(1.5)
        check("the File Manager and Notes coexist", state()[1:3] == (3, 2))
        shot("03-notes")
        mouse(260, 60, True)             # drag Notes by its title bar
        mouse(308, 92)
        mouse(308, 92, False)
        shot("04-dragged")
        click(578, 96)                   # its close button
        check("mouse closes dragged Notes window", state()[1] == 1)
        m.key("f3")
        check("System window opens", state()[1] == 5)
        shot("05-system")
        m.key("f10")
        check("Shut Down dialog is graphical", state()[0] == 2)
        shot("06-shutdown")
        m.key("esc")
        check("Escape cancels Shut Down", state()[0] == 1)

        # ---- the WinKit terminal ----
        m.key("f6")
        check("F6 opens Terminal", state()[1] == 13 and state()[2] == 4)
        out = run("dir")
        check("dir lists the volume",
              "README.TXT" in out and "HELLO.WKX" in out and "NOTES.TXT" in out)
        shot("07-terminal-dir")
        out = run("type notes.txt")
        check("a note saved by the desktop reads back through the filesystem",
              "Assembly GUI notes" in out)
        out = run("hello")
        check("native program runs and prints",
              "Hello from a native WinKit program!" in out)
        out = run("count")
        check("program output and loop work", "1 2 3 4 5" in out and "20" in out)
        out = run("fib")
        check("FIB: subroutines and division work",
              "0 1 1 2 3 5 8 13 21 34 55 89 144" in out)
        out = run("hi")
        check("quick-start program prints", "Hi from WinKit!" in out)
        m.type("cls"); m.key("ret"); time.sleep(.3)
        m.type("reverse"); m.key("ret"); time.sleep(.6)
        m.type("winkit"); m.key("ret"); time.sleep(.6)
        out = term_text()
        check("AH=3 reads keys: REVERSE prints the line backwards",
              "Type a line: winkit" in out and "tikniw" in out)
        run("echo winkit works > T.TXT")
        out = run("type t.txt")
        check("echo > FILE creates a file", "winkit works" in out)
        run("ren t.txt u.txt")
        out = run("dir")
        check("ren renames", "U.TXT" in out and "T.TXT" not in out)
        run("copy u.txt v.txt")
        out = run("dir")
        check("copy duplicates", "V.TXT" in out and "U.TXT" in out)
        run("del u.txt")
        run("del v.txt")
        out = run("dir")
        check("del removes files", "U.TXT" not in out and "V.TXT" not in out)
        out = run("bogus")
        check("unknown command is reported", "Bad command" in out)
        out = run("ver")
        check("ver identifies WinKit", "WinKit" in out)

        # ---- the WinKit Store: a download over the serial port ----
        # WKSTORE.WKX draws its own window (INT 80h 10h..15h).
        m.key("f7")
        time.sleep(2)
        click(CX + 100, CY + 43 + 18 + 5)   # second row of the list: DICE
        click(CX + 40, CY + 14)            # the Install button
        time.sleep(2)
        shot("08-store")
        check("the Store is a program with its own window",
              gui() == (1, "WinKit Store"))
        m.key("up")                          # first row: CLOCK
        m.key("ret")                         # Enter installs too
        time.sleep(2)
        click(CX + 108, CY + 14)           # the Run button
        time.sleep(1.5)
        check("Run in the Store starts the installed program in a window",
              gui() == (1, "Clock"))
        shot("09-store-app")
        click(WIN_X + 416 - 13, WIN_Y + 11)      # the window's close button
        time.sleep(1)
        check("closing it brings the desktop back",
              gui()[0] == 0 and state()[1] == 13 and state()[2] == 4)
        out = run("dir")
        check("the Store installs the program clicked with the mouse",
              "DICE.WKX" in out)
        check("the Store installs the program picked with the keyboard",
              "CLOCK.WKX" in out)
        # ---- the File Manager: nine files to a page; the 13 on the disk
        # are README.TXT, COUNT, FIB, HELLO, HI, KEYS, REVERSE, SETUP and
        # WINDOW, then WKSTORE, NOTES.TXT, DICE and CLOCK on page two ----
        free_kb = int(out.split(" KB free")[0].split()[-1])
        m.key("f1")
        time.sleep(1)
        click(*fm_row(0))
        check("a click in the File Manager selects a file", fm()[:2] == (0, 0))
        check("it shows the same free disk space as dir",
              fm()[2] // 2 == free_kb and 1000 < free_kb < 1440)
        shot("10-file-manager")
        dblclick(*fm_row(3))                 # HELLO.WKX
        time.sleep(1.5)
        check("a double click on a text program runs it in the terminal",
              state()[2] == 4 and
              "Hello from a native WinKit program!" in term_text())
        m.key("f1")
        m.key("pgdn")
        dblclick(*fm_row(2))                 # DICE.WKX
        time.sleep(1.5)
        check("a double click on a windowed program opens its window",
              gui() == (1, "Dice"))
        m.key("esc")
        time.sleep(1)
        click(*fm_row(1))                    # NOTES.TXT
        click(*fm_button(0))                 # Open
        time.sleep(1)
        check("Open shows a text file in the terminal",
              state()[2] == 4 and "type NOTES.TXT" in term_text() and
              "Assembly GUI notes" in term_text())
        m.key("f1")
        click(*fm_row(1))
        click(*fm_button(1))                 # Copy: it asks for the name
        asking = fm()[1]
        m.type("n2.txt")
        m.key("ret")
        time.sleep(1)
        m.key("f6")
        out = run("type n2.txt")
        check("Copy makes a copy under the name typed",
              asking == 2 and "Assembly GUI notes" in out)
        m.key("f1")
        click(*fm_row(4))                    # N2.TXT, the newest file
        click(*fm_button(2))                 # Rename: starts from the old name
        asking = fm()[1]
        for _ in range(6):
            m.key("backspace")
        m.type("n3.txt")
        shot("11-file-manager-rename")
        m.key("ret")
        time.sleep(1)
        m.key("f6")
        out = run("dir")
        check("Rename gives the file the name typed",
              asking == 1 and "N3.TXT" in out and "N2.TXT" not in out)
        m.key("f1")
        click(*fm_row(4))
        click(*fm_button(3))                 # Delete: it asks first
        asking = fm()[1]
        m.key("n")                           # no
        time.sleep(.5)
        kept = fm()[1] == 0 and fm()[0] == 13
        m.key("delete")                      # the key asks too
        m.key("y")
        time.sleep(1)
        m.key("f6")
        out = run("dir")
        check("Delete removes the file, but only after a yes",
              asking == 3 and kept and "N3.TXT" not in out and
              fm()[2] // 2 == free_kb)
        click(30, 466)                       # Start
        click(100, 398)                      # Programs: its list opens
        shot("12-programs")
        click(178 + 60, 266 + 8 * 18 + 9)    # DICE, the 9th program of 10
        time.sleep(1.5)
        check("Start > Programs lists the installed programs and runs one",
              gui() == (1, "Dice"))
        m.key("esc")
        time.sleep(1)
        run("window")                        # a program with its own window
        click(CX + 60, CY + 30)            # its Quit button
        time.sleep(1)
        check("a windowed program gets its click and returns",
              "The window is closed." in term_text())
        shot("13-terminal-final")
        out = run("dir")
        m.type("cls")
        m.key("ret")
        time.sleep(.5)
        text = term_text()
        check("cls clears the screen",
              text.splitlines()[0].startswith("winkit>") and "README" not in text)

        # ---- restart: the saved file must survive a reboot ----
        m.key("f10")
        click(DLG + 310, 226)                 # "Restart"
        click(DLG + 256, 332)                 # OK
        time.sleep(2)
        wait_gui()
        state_addr, term_addr = locate()
        check("Restart reboots into the desktop", state()[0] == 0)
        click(DLG + 330, 320)
        shot("14-shortcuts")
        dblclick(124, 96 + 24)               # second column: SETUP, DICE, CLOCK
        time.sleep(1.5)
        check("an installed program has a desktop shortcut that runs it",
              gui() == (1, "Dice"))
        m.key("esc")
        time.sleep(1)
        m.key("f6")
        out = run("type notes.txt")
        check("files survive the reboot", "Assembly GUI notes" in out)
        shot("15-after-reboot")

    # ---- Setup: install on an empty hard disk, then start from it ----
    hda = ROOT / "out" / "test-hda.img"
    with open(hda, "wb") as fh:
        fh.truncate(16 * 1024 * 1024)
    with Machine(image, hda=str(hda),
                 boot="order=ac") as m:
        memory = Path(m.tmp) / "ram.bin"
        wait_gui()
        state_addr, term_addr = locate()
        click(DLG + 330, 320)                      # OK on the welcome dialog
        dblclick(124, 16 + 24)               # the Setup shortcut
        time.sleep(1.5)
        check("Setup has a desktop shortcut that opens it",
              gui() == (1, "WinKit Setup"))
        def boot_drive():
            """The BIOS drive WinKit started from (boot sector at 7C00)."""
            m.qmp("pmemsave", val=0x7c24, size=1, filename=str(memory))
            return memory.read_bytes()[0]

        check("it was started from the floppy", boot_drive() == 0)
        m.key("ret")                         # Install
        m.key("ret")                         # ...and again to confirm
        time.sleep(1)
        shot("16-setup")
        m.key("ret")                         # read after the copy: Restart
        deadline = time.time() + 180
        while boot_drive() != 0x80:
            assert time.time() < deadline, "no restart from the hard disk"
            time.sleep(1)
        time.sleep(2)
        wait_gui()
        state_addr, term_addr = locate()
        check("Restart in Setup starts WinKit from the hard disk, with the "
              "floppy still in the drive",
              state()[0] == 0 and boot_drive() == 0x80)
        m.key("ret")
        m.key("f6")
        out = run("dir")
        check("the hard disk holds the system's files",
              "SETUP.WKX" in out and "WKSTORE.WKX" in out)
        run("echo on the hard disk > HD.TXT", wait=2)   # slower than a floppy
        out = run("type hd.txt")
        check("files can be written on the hard disk",
              "on the hard disk" in out)
        m.type("cls")
        m.key("ret")
        m.type("setup")
        m.key("ret")
        time.sleep(1.5)
        shot("17-from-hard-disk")
        m.key("ret")                         # must not offer to install
        m.key("ret")
        m.key("esc")
        time.sleep(1.5)
        check("Setup does nothing when WinKit already runs from the hard "
              "disk", gui()[0] == 0 and "HD.TXT" in run("dir"))
    hda.unlink()

    # ---- the image itself on other boot media, as on a real PC ----
    # Written to a hard disk or a USB stick, the BIOS starts it as drive 80h
    # with a shape that is not a floppy's (and that it cannot always report),
    # so the boot sector and the kernel must read it by sector number.
    usb = ("-usb", "-drive", f"if=none,id=stick,format=raw,file={image}",
           "-device", "usb-storage,drive=stick,bootindex=0")
    for media, options in (("a hard disk", {"hda": image, "boot": "c"}),
                           ("a USB stick", {"extra": usb})):
        with Machine(None, **options) as m:
            memory = Path(m.tmp) / "ram.bin"
            wait_gui()
            state_addr, term_addr = locate()
            m.qmp("pmemsave", val=0x7c25, size=1, filename=str(memory))
            by_sector = memory.read_bytes()[0]
            m.key("ret")
            m.key("f6")
            out = run("dir", wait=2)
            check(f"the image starts from {media} and reads its files",
                  boot_drive() == 0x80 and by_sector == 1 and
                  "WKSTORE.WKX" in out and "KB free" in out)

    # ---- the real-PC route: start from a USB stick, install on the disk ----
    hda = ROOT / "out" / "test-hda.img"
    with open(hda, "wb") as fh:
        fh.truncate(16 * 1024 * 1024)
    with Machine(None, hda=str(hda), extra=usb) as m:
        memory = Path(m.tmp) / "ram.bin"
        wait_gui()
        state_addr, term_addr = locate()
        check("started from the stick, which the BIOS numbers 80h",
              boot_drive() == 0x80)
        m.key("ret")
        m.key("f6")
        m.type("setup")
        m.key("ret")
        time.sleep(2)
        for _ in range(3):                   # Install, confirm, Restart
            m.key("ret")
        deadline = time.time() + 240
        while boot_drive() != 0x81:
            assert time.time() < deadline, "no restart from the hard disk"
            time.sleep(1)
        time.sleep(2)
        wait_gui()
        state_addr, term_addr = locate()
        m.key("ret")
        m.key("f6")
        out = run("dir", wait=2)
        check("Setup started from a USB stick installs on the PC's hard "
              "disk (81h) and restarts from it",
              boot_drive() == 0x81 and "SETUP.WKX" in out)
    hda.unlink()

    print(f"{checks}/{checks} WinKit checks passed; screenshots: {SHOTS}")


if __name__ == "__main__":
    main()
