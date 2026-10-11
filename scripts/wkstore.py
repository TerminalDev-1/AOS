#!/usr/bin/env python3
"""The WinKit Store server: the host side of WKSTORE.WKX.

WinKit has no network, so its store program talks to the machine's serial
port (COM1). This script sits on the other end of that port and serves the
programs in store/: every store/NAME.ASM is assembled with wkasm and offered
as NAME.WKX. A source describes itself with a comment line such as

    ; Store: Guess the number

QEMU must expose the serial port as a TCP server, which is what
`python scripts/run.py --store` does (it also starts this server). To attach
to a QEMU you started yourself:

    qemu-system-i386 ... -serial tcp:127.0.0.1:7798,server=on,wait=off
    python scripts/wkstore.py

Protocol. WinKit sends a request line, the server answers:

    L\\n          the catalogue: one 42-byte record per program
                 (name 9 bytes NUL padded, size word, description 31 bytes)
    G NAME \\n    the program (written without the spaces: GNAME)

An answer is b"K", the length (16-bit little endian), the bytes, and the
16-bit sum of the bytes; or b"E" when the request cannot be served.

Usage: python scripts/wkstore.py [--port 7798] [--dir store] [--list]
"""
import argparse
import os
import re
import socket
import struct
import time

import wkasm

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STORE = os.path.join(ROOT, "store")
PORT = 7798

MAX_APPS = 64                         # these match WKSTORE.ASM
NAME_LEN = 8
DESC_LEN = 30
MAX_FILE = 0xD000


def describe(path):
    """The text of the first '; Store: ...' line, or an empty string."""
    with open(path, encoding="latin-1") as fh:
        for line in fh:
            mo = re.match(r"\s*;\s*Store:\s*(.*)", line, re.I)
            if mo:
                return mo.group(1).strip()
    return ""


def load_catalog(directory=STORE, log=print):
    """{NAME: (description, binary)} for every program that assembles."""
    apps = {}
    if not os.path.isdir(directory):
        return apps
    for filename in sorted(os.listdir(directory), key=str.upper):
        stem, ext = os.path.splitext(filename)
        if ext.upper() != ".ASM":
            continue
        name = stem.upper()
        if not re.fullmatch(r"[A-Z0-9_]{1,%d}" % NAME_LEN, name):
            log(f"[store] skipping {filename}: not an 8-character name")
            continue
        path = os.path.join(directory, filename)
        try:
            data, _, _ = wkasm.assemble_file(path)
        except wkasm.AsmError as e:
            log(f"[store] skipping {filename}: {e}")
            continue
        if not 0 < len(data) <= MAX_FILE:
            log(f"[store] skipping {filename}: {len(data)} bytes")
            continue
        if len(apps) == MAX_APPS:
            log(f"[store] more than {MAX_APPS} programs; the rest are left out")
            break
        apps[name] = (describe(path), data)
    return apps


def reply(payload):
    return (b"K" + struct.pack("<H", len(payload)) + payload +
            struct.pack("<H", sum(payload) & 0xFFFF))


def catalog_bytes(apps):
    out = bytearray()
    for name, (text, data) in apps.items():
        out += name.encode("ascii").ljust(NAME_LEN + 1, b"\0")
        out += struct.pack("<H", len(data))
        text = text.encode("ascii", "replace")[:DESC_LEN]
        out += text.ljust(DESC_LEN + 1, b"\0")
    return bytes(out)


def answer(request, directory=STORE, log=print):
    """The bytes to send back for one request line."""
    apps = load_catalog(directory, log)     # re-read, so new programs show up
    if request == b"L":
        log(f"[store] catalogue: {len(apps)} program(s)")
        return reply(catalog_bytes(apps))
    if request[:1] == b"G":
        name = request[1:].decode("ascii", "replace").upper()
        if name in apps:
            data = apps[name][1]
            log(f"[store] {name}.WKX: {len(data)} bytes")
            return reply(data)
        log(f"[store] {name}: no such program")
    return b"E"


def serve(port=PORT, directory=STORE, host="127.0.0.1", log=print, wait=30):
    """Connect to QEMU's serial port and serve until QEMU goes away."""
    deadline = time.time() + wait
    while True:
        try:
            sock = socket.create_connection((host, port), 2)
            break
        except OSError:
            if time.time() > deadline:
                log(f"[store] no serial port at {host}:{port}; is QEMU "
                    "running with -serial tcp:...,server=on?")
                return
            time.sleep(0.3)
    sock.settimeout(None)
    log(f"[store] serving {directory} on the serial port ({host}:{port})")
    line = bytearray()
    with sock:
        while True:
            try:
                chunk = sock.recv(256)
            except OSError:
                chunk = b""
            if not chunk:
                log("[store] the serial port closed")
                return
            for byte in chunk:
                if byte == 10:
                    try:
                        sock.sendall(answer(bytes(line), directory, log))
                    except OSError:
                        return
                    line.clear()
                elif byte != 13 and len(line) < 64:
                    line.append(byte)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=PORT,
                    help="TCP port of QEMU's serial port")
    ap.add_argument("--dir", default=STORE, help="directory of .ASM programs")
    ap.add_argument("--list", action="store_true",
                    help="print the catalogue and exit")
    args = ap.parse_args()
    if args.list:
        for name, (text, data) in load_catalog(args.dir).items():
            print(f"{name:<9}{len(data):>5}  {text}")
        return
    serve(args.port, args.dir)


if __name__ == "__main__":
    main()
