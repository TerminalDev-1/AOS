"""Exercise WinKit in QEMU: boot, desktop, filesystem, terminal, programs.

There is no DOS in the image, so everything below is the system's own code:
the desktop, the FAT12 driver and the WinKit terminal.
"""
import struct
import threading
import time
from pathlib import Path

import wkstore
from qemu import Machine, free_port

ROOT = Path(__file__).resolve().parent.parent
SHOTS = ROOT / "out" / "screens" / "gui"


def main():
    SHOTS.mkdir(parents=True, exist_ok=True)
    checks = 0
    store_port = free_port()             # the store server, on the serial port
    serial = ("-serial", f"tcp:127.0.0.1:{store_port},server=on,wait=off")
    with Machine(str(ROOT / "out" / "winkit.img"), extra=serial) as m:
        threading.Thread(target=wkstore.serve, args=(store_port,),
                         kwargs={"log": lambda text: None},
                         daemon=True).start()
        memory = Path(m.tmp) / "ram.bin"

        def wait_gui():
            deadline = time.time() + 30
            while m.video_mode() != 0x12:
                if time.time() > deadline:
                    raise AssertionError("desktop did not start")
                time.sleep(.2)
            time.sleep(1)

        def locate():
            m.qmp("pmemsave", val=0, size=640 * 1024, filename=str(memory))
            ram = memory.read_bytes()
            state = ram.rfind(b"V4GUIstate") + 10
            term = ram.rfind(b"WKTERMBUF") + 9
            assert state > 10 and term > 9, "telemetry not found"
            return state, term

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
            s = state()
            m.qmp("input-send-event", events=[
                {"type": "rel", "data": {"axis": "x", "value": x - s[5]}},
                {"type": "rel", "data": {"axis": "y", "value": y - s[6]}}])
            time.sleep(.2)
            if down is not None:
                m.qmp("input-send-event", events=[
                    {"type": "btn", "data": {"button": "left", "down": down}}])
                time.sleep(.2)

        def click(x, y):
            mouse(x, y, True)
            mouse(x, y, False)
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
        check("boots straight into the VGA desktop", state()[0] == 0)
        check("PS/2 mouse enabled without any driver", state()[4] == 1)
        shot("01-welcome")
        click(330, 320)                  # OK on the welcome dialog
        check("mouse enters desktop", state()[0] == 1)
        click(30, 466)                   # Start button
        click(100, 258)                  # Files in the Start menu
        check("Files opens from the Start menu", state()[1] == 1)
        m.key("pgdn")
        shot("02-files-page-two")
        m.key("pgup")
        m.key("f2")
        m.type("Assembly GUI notes")
        m.key("f4")
        time.sleep(1.5)
        check("Files and Notes coexist", state()[1:3] == (3, 2))
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
        check("F6 opens WinKit", state()[1] == 13 and state()[2] == 4)
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
        # WKSTORE.WKX draws its own window (INT 80h 10h..14h) at 112,72; its
        # client area starts 3,21 inside that.
        m.key("f7")
        time.sleep(2)
        click(115 + 100, 93 + 43 + 18 + 5)   # second row of the list: DICE
        click(115 + 40, 93 + 14)             # the Install button
        time.sleep(2)
        shot("08-store")
        check("the Store is a program with its own window",
              gui() == (1, "WinKit Store"))
        m.key("up")                          # first row: CLOCK
        m.key("ret")                         # Enter installs too
        time.sleep(2)
        click(115 + 108, 93 + 14)            # the Run button
        time.sleep(1.5)
        check("Run in the Store starts the installed program in a window",
              gui() == (1, "Clock"))
        shot("09-store-app")
        click(112 + 416 - 13, 72 + 11)       # the window's close button
        time.sleep(1)
        check("closing it brings the desktop back",
              gui()[0] == 0 and state()[1] == 13 and state()[2] == 4)
        out = run("dir")
        check("the Store installs the program clicked with the mouse",
              "DICE.WKX" in out)
        check("the Store installs the program picked with the keyboard",
              "CLOCK.WKX" in out)
        m.key("f1")                          # Files lists them: 10 per column
        time.sleep(1)
        click(96 + 12 + 200 + 40, 20 + 64 + 18 + 8)  # DICE.WKX, the 12th
        time.sleep(1.5)
        check("clicking a program in Files runs it", gui() == (1, "Dice"))
        m.key("esc")
        time.sleep(1)
        click(96 + 12 + 40, 20 + 64 + 3 * 18 + 8)    # HELLO.WKX, the 4th
        time.sleep(1.5)
        check("a text program started from Files gets the terminal",
              state()[2] == 4 and
              "Hello from a native WinKit program!" in term_text())
        click(30, 466)                       # Start
        click(100, 398)                      # Programs: its list opens
        shot("10-programs")
        click(178 + 60, 266 + 8 * 18 + 9)    # DICE, the 9th program of 10
        time.sleep(1.5)
        check("Start > Programs lists the installed programs and runs one",
              gui() == (1, "Dice"))
        m.key("esc")
        time.sleep(1)
        run("window")                        # a program with its own window
        click(115 + 60, 93 + 30)             # its Quit button
        time.sleep(1)
        check("a windowed program gets its click and returns",
              "The window is closed." in term_text())
        shot("11-terminal-final")
        out = run("dir")
        m.type("cls")
        m.key("ret")
        time.sleep(.5)
        text = term_text()
        check("cls clears the screen",
              text.splitlines()[0].startswith("winkit>") and "README" not in text)

        # ---- restart: the saved file must survive a reboot ----
        m.key("f10")
        click(310, 226)                  # "Restart"
        click(256, 332)                  # OK
        time.sleep(2)
        wait_gui()
        state_addr, term_addr = locate()
        check("Restart reboots into the desktop", state()[0] == 0)
        click(330, 320)
        m.key("f6")
        out = run("type notes.txt")
        check("files survive the reboot", "Assembly GUI notes" in out)
        shot("12-after-reboot")

    # ---- Setup: install on an empty hard disk, then start from it ----
    hda = ROOT / "out" / "test-hda.img"
    with open(hda, "wb") as fh:
        fh.truncate(16 * 1024 * 1024)
    with Machine(str(ROOT / "out" / "winkit.img"), hda=str(hda),
                 boot="order=ac") as m:
        memory = Path(m.tmp) / "ram.bin"
        wait_gui()
        state_addr, term_addr = locate()
        m.key("ret")
        m.key("f6")
        m.type("setup")
        m.key("ret")
        time.sleep(1.5)
        check("Setup opens its window", gui() == (1, "WinKit Setup"))
        m.key("ret")                         # Install
        m.key("ret")                         # ...and again to confirm
        time.sleep(1)
        shot("13-setup")
        m.key("esc")                         # read once the copy is over
        deadline = time.time() + 180
        while gui()[0]:
            assert time.time() < deadline, "Setup did not finish"
            time.sleep(1)
        floppy = [d["device"] for d in m.qmp("query-block")
                  if "floppy" in d["device"]][0]
        m.qmp("eject", device=floppy, force=True)
        m.qmp("system_reset")
        time.sleep(3)
        wait_gui()
        state_addr, term_addr = locate()
        check("with the floppy out, the machine starts WinKit from the "
              "hard disk", state()[0] == 0)
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
        shot("14-from-hard-disk")
        m.key("ret")                         # must not offer to install
        m.key("ret")
        m.key("esc")
        time.sleep(1.5)
        check("Setup does nothing when WinKit already runs from the hard "
              "disk", gui()[0] == 0 and "HD.TXT" in run("dir"))
    hda.unlink()

    print(f"{checks}/{checks} WinKit checks passed; screenshots: {SHOTS}")


if __name__ == "__main__":
    main()
