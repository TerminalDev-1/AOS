"""Headless QEMU driver: boot an image, type at it, grab the screen.

QEMU is started with no display and a QMP control socket. Keystrokes and
mouse movement go in through the emulated devices; guest memory is read with
pmemsave, so tests see what a person at the machine would see.
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

KEYS = {
    " ": "spc", "\n": "ret", "\t": "tab", "-": "minus", "=": "equal",
    ",": "comma", ".": "dot", "/": "slash", "\\": "backslash",
    ";": "semicolon", "'": "apostrophe", "[": "bracket_left",
    "]": "bracket_right", "`": "grave_accent",
}
SHIFTED = {
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7",
    "*": "8", "(": "9", ")": "0", "_": "minus", "+": "equal", ":": "semicolon",
    '"': "apostrophe", "<": "comma", ">": "dot", "?": "slash",
    "|": "backslash", "{": "bracket_left", "}": "bracket_right",
    "~": "grave_accent",
}


def find_qemu():
    cands = [os.environ.get("QEMU"), shutil.which("qemu-system-i386"),
             r"C:\Program Files\qemu\qemu-system-i386.exe"]
    for c in cands:
        if c and os.path.isfile(c):
            return c
    sys.exit("qemu-system-i386 not found; install QEMU or set QEMU=/path")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Machine:
    def __init__(self, floppy, mem=4, cpu="486", snapshot=True, extra=(),
                 hda=None, boot="a"):
        self.port = free_port()
        self.tmp = tempfile.mkdtemp(prefix="winkitqemu")
        cmd = [find_qemu(), "-M", "pc", "-cpu", cpu, "-m", str(mem),
               "-display", "none", "-vga", "std", "-net", "none",
               "-rtc", "base=1988-11-30T12:00:00",
               "-boot", boot,
               "-qmp", f"tcp:127.0.0.1:{self.port},server=on,wait=off"]
        if floppy:
            cmd += ["-drive", f"file={floppy},format=raw,if=floppy,index=0"]
        if hda:
            cmd += ["-drive", f"file={hda},format=raw,if=ide,index=0"]
        if snapshot:
            cmd.append("-snapshot")      # never write to the image file
        cmd += list(extra)
        self.cmdline = cmd
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.PIPE)
        self.sock = None
        deadline = time.time() + 20
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("qemu exited: " +
                                   self.proc.stderr.read().decode(errors="replace"))
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), 2)
                break
            except OSError:
                time.sleep(0.2)
        if not self.sock:
            raise RuntimeError("could not connect to QMP")
        self.rfile = self.sock.makefile("r", encoding="utf-8")
        self.rfile.readline()                       # greeting
        self.qmp("qmp_capabilities")

    def qmp(self, execute, **arguments):
        msg = {"execute": execute}
        if arguments:
            msg["arguments"] = arguments
        self.sock.sendall((json.dumps(msg) + "\n").encode())
        while True:
            line = self.rfile.readline()
            if not line:
                raise RuntimeError("QMP connection closed")
            resp = json.loads(line)
            if "return" in resp:
                return resp["return"]
            if "error" in resp:
                raise RuntimeError(f"QMP {execute}: {resp['error']}")

    def key(self, *names, hold=40):
        self.qmp("send-key", keys=[{"type": "qcode", "data": n} for n in names],
                 **{"hold-time": hold})
        time.sleep(hold / 1000 + 0.03)

    def type(self, text):
        for ch in text:
            if ch in KEYS:
                self.key(KEYS[ch])
            elif ch in SHIFTED:
                self.key("shift", SHIFTED[ch])
            elif ch.isupper():
                self.key("shift", ch.lower())
            else:
                self.key(ch)

    def video_mode(self):
        """BIOS video mode byte (12h while the desktop is up)."""
        path = os.path.join(self.tmp, "mode.bin")
        self.qmp("pmemsave", val=0x449, size=1, filename=path)
        with open(path, "rb") as fh:
            return fh.read()[0]

    def screenshot(self, path):
        self.qmp("screendump", filename=path, format="png")

    def close(self):
        try:
            self.qmp("quit")
        except Exception:
            pass
        try:
            self.proc.wait(5)
        except Exception:
            self.proc.kill()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
