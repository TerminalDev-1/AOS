#!/usr/bin/env python3
"""Start the built WinKit image in an interactive QEMU window.

Usage: python scripts/run.py [--image out/winkit.img] [--hda disk.img]
                            [--boot a|c] [--snapshot] [--store]
                            [--fullscreen]

The WinKit screen (640x480) is scaled to fill the QEMU window, whatever
size you make it; --fullscreen starts on the whole monitor (Ctrl+Alt+F
leaves full screen, Ctrl+Alt+G releases the mouse).

--hda attaches a hard disk image (created empty, 16 MB, if the file does
not exist) so that Start > Programs > SETUP can install WinKit on it.
--boot c then starts WinKit from that disk, with no floppy in the machine.

--store connects the machine's serial port to the WinKit Store server
(scripts/wkstore.py), so that `wkstore` in the terminal can download the
programs in store/.
"""
import argparse
import os
import subprocess
import threading

import wkstore
from qemu import find_qemu

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDA_BYTES = 16 * 1024 * 1024            # size of a new --hda image


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--image", default=os.path.join(ROOT, "out", "winkit.img"))
    ap.add_argument("--hda", help="raw hard disk image to attach (the first "
                                  "hard disk); created if it is missing")
    ap.add_argument("--boot", default="a", choices=["a", "c"])
    ap.add_argument("--snapshot", action="store_true",
                    help="discard all disk writes when QEMU exits")
    ap.add_argument("--store", action="store_true",
                    help="serve the programs in store/ to wkstore")
    ap.add_argument("--store-port", type=int, default=wkstore.PORT,
                    help="TCP port used for the serial link to the store")
    ap.add_argument("--fullscreen", action="store_true",
                    help="fill the whole monitor (Ctrl+Alt+F to leave)")
    args = ap.parse_args()
    display = "gtk,zoom-to-fit=on"         # scale the screen to the window
    if args.fullscreen:
        display += ",full-screen=on,show-menubar=off"
    cmd = [find_qemu(), "-M", "pc", "-cpu", "486", "-m", "4", "-net", "none",
           "-boot", args.boot, "-display", display]
    if args.boot == "c":                   # from the hard disk: no floppy
        if not args.hda:
            ap.error("--boot c needs --hda")
    else:
        cmd += ["-drive", f"file={args.image},format=raw,if=floppy,index=0"]
    if args.hda:
        if not os.path.exists(args.hda):
            with open(args.hda, "wb") as fh:
                fh.truncate(HDA_BYTES)
            print(f"[run] created an empty hard disk: {args.hda}")
        cmd += ["-drive", f"file={args.hda},format=raw,if=ide,index=0"]
    if args.snapshot:
        cmd.append("-snapshot")
    if args.store:
        cmd += ["-serial",
                f"tcp:127.0.0.1:{args.store_port},server=on,wait=off"]
        threading.Thread(target=wkstore.serve, args=(args.store_port,),
                         daemon=True).start()
    subprocess.call(cmd)


if __name__ == "__main__":
    main()
