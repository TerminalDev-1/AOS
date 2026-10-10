"""Assemble WinKit with its own assembler (scripts/wkasm.py).

Produces, in out/bin:
    KERNEL.BIN   the desktop + terminal kernel (src/GUI.ASM), ORG 100h
    BOOT.BIN     the 512-byte boot sector (src/BOOT.ASM)
    *.WKX        native programs (src/progs/*.ASM)

Pure Python: no DOS, no DOSBox, no third-party assembler.
"""
from pathlib import Path

import wkasm

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
OUT = ROOT / "out"


def build_all():
    out = OUT / "bin"
    out.mkdir(parents=True, exist_ok=True)
    jobs = [(SRC / "GUI.ASM", "KERNEL.BIN"), (SRC / "BOOT.ASM", "BOOT.BIN")]
    for program in sorted((SRC / "progs").glob("*.ASM")):
        jobs.append((program, program.stem.upper() + ".WKX"))
    result = {}
    for source, target in jobs:
        try:
            data, _, _ = wkasm.assemble_file(str(source))
        except wkasm.AsmError as e:
            raise SystemExit(f"wkasm: {e}")
        (out / target).write_bytes(data)
        result[target] = data
    print("[build] " + ", ".join(f"{k} {len(v)}" for k, v in result.items()))
    return result


if __name__ == "__main__":
    build_all()
