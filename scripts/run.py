#!/usr/bin/env python3
"""Start the built WinKit image in an interactive QEMU window.

Usage: python scripts/run.py [--image out/winkit.img] [--hda disk.img]
                            [--boot a|c] [--snapshot]
"""
import argparse
import os
import subprocess

from qemu import find_qemu

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--image", default=os.path.join(ROOT, "out", "winkit.img"))
    ap.add_argument("--hda", help="raw hard disk image to attach as C:")
    ap.add_argument("--boot", default="a", choices=["a", "c"])
    ap.add_argument("--snapshot", action="store_true",
                    help="discard all disk writes when QEMU exits")
    args = ap.parse_args()
    cmd = [find_qemu(), "-M", "pc", "-cpu", "486", "-m", "4", "-net", "none",
           "-drive", f"file={args.image},format=raw,if=floppy,index=0",
           "-boot", args.boot]
    if args.hda:
        cmd += ["-drive", f"file={args.hda},format=raw,if=ide,index=0"]
    if args.snapshot:
        cmd.append("-snapshot")
    subprocess.call(cmd)


if __name__ == "__main__":
    main()
