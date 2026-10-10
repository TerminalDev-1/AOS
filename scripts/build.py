"""Assemble WinKit with the MASM 5.10 tool chain.

Produces, in out/bin:
    KERNEL.BIN   the desktop + terminal kernel (src/GUI.ASM), ORG 100h
    BOOT.BIN     the 512-byte boot sector (src/BOOT.ASM)
    *.WKX        native programs (src/progs/*.ASM)

MASM.EXE, LINK.EXE and EXE2BIN.EXE are 16-bit DOS programs. They run under
DOSBox-X on the build machine; nothing from DOS ends up on the disk.

Where the tools come from: put the three files in ./tools, or point
WINKIT_TOOLS at a folder that has them. They are in Microsoft's MS-DOS
repository (MIT licensed) under v4.0/src/TOOLS:
    https://github.com/microsoft/MS-DOS
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
OUT = ROOT / "out"
TOOLS = ("MASM.EXE", "LINK.EXE", "EXE2BIN.EXE")

CONF = """\
[sdl]
output=surface
[dosbox]
memsize=16
quit warning=false
[log]
logfile=
[cpu]
core=dynamic
cputype=386
cycles=max
turbo=true
[dos]
ver=5.0
lfn=false
files=60
[render]
frameskip=10
[mixer]
nosound=true
[autoexec]
@echo off
mount d "{stage}" -nocachedir
d:
cd \\
call BUILD.BAT
exit
"""


def find_dosbox():
    cands = [os.environ.get("DOSBOX_X"), shutil.which("dosbox-x"),
             r"C:\DOSBox-X\dosbox-x.exe",
             r"C:\Program Files\DOSBox-X\dosbox-x.exe",
             "/usr/bin/dosbox-x", "/usr/local/bin/dosbox-x",
             "/Applications/dosbox-x.app/Contents/MacOS/dosbox-x"]
    for c in cands:
        if c and os.path.isfile(c):
            return c
    sys.exit("DOSBox-X not found; install it or set DOSBOX_X=/path/to/dosbox-x")


def find_tools():
    folder = Path(os.environ.get("WINKIT_TOOLS", ROOT / "tools"))
    missing = [n for n in TOOLS if not (folder / n).is_file()]
    if missing:
        sys.exit(f"{', '.join(missing)} not found in {folder}.\n"
                 "Copy MASM.EXE, LINK.EXE and EXE2BIN.EXE there (or set "
                 "WINKIT_TOOLS). They are in\n"
                 "https://github.com/microsoft/MS-DOS under v4.0/src/TOOLS.")
    return folder


def build_all():
    tools = find_tools()
    work = OUT / "work"
    if work.exists():
        shutil.rmtree(work)
    (work / "TOOLS").mkdir(parents=True)
    for name in TOOLS:
        shutil.copyfile(tools / name, work / "TOOLS" / name)

    def stage(source):
        # the 1988 tools expect CR LF line ends
        (work / source.name.upper()).write_bytes(
            source.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))

    for source in sorted(SRC.iterdir()):
        if source.suffix.upper() in (".ASM", ".INC"):
            stage(source)
    programs = sorted((SRC / "progs").glob("*.ASM"))
    for source in programs:
        stage(source)

    # The kernel and programs are .COM images (ORG 100h); the boot sector is
    # a plain .BIN (ORG 0).
    jobs = [("GUI", "COM"), ("BOOT", "BIN")]
    jobs += [(p.stem.upper(), "COM") for p in programs]
    lines = ["@echo off", "set PATH=D:\\TOOLS;Z:\\;Z:\\SYSTEM"]
    for name, ext in jobs:
        lines += [f"masm {name}.ASM,{name}.OBJ,NUL,NUL; >> BUILD.LOG",
                  f"link {name}.OBJ,{name}.EXE,NUL; >> BUILD.LOG",
                  f"exe2bin {name}.EXE {name}.{ext} >> BUILD.LOG"]
    (work / "BUILD.BAT").write_text("\r\n".join(lines) + "\r\n")
    config = work / "DOSBOX.CNF"
    config.write_text(CONF.format(stage=str(work)))
    subprocess.run([find_dosbox(), "-conf", str(config), "-nopromptfolder",
                    "-fastlaunch", "-exit", "-silent"], timeout=180, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    log = work / "BUILD.LOG"
    log_text = log.read_text(encoding="cp437") if log.exists() else ""
    missing = [f"{n}.{e}" for n, e in jobs if not (work / f"{n}.{e}").exists()]
    if missing or re.search(r"[1-9]\d* Severe Errors|error [A-Z]\d+|fatal|error L\d+",
                            log_text, re.I):
        raise RuntimeError(f"build failed (missing: {missing})\n{log_text}")

    out = OUT / "bin"
    out.mkdir(parents=True, exist_ok=True)
    result = {}
    for name, ext in jobs:
        data = (work / f"{name}.{ext}").read_bytes()
        target = ("KERNEL.BIN" if name == "GUI" else
                  "BOOT.BIN" if name == "BOOT" else f"{name}.WKX")
        (out / target).write_bytes(data)
        result[target] = data
    print("[build] " + ", ".join(f"{k} {len(v)}" for k, v in result.items()))
    return result


if __name__ == "__main__":
    build_all()
