"""Host-side serial catalogue for native AOS apps on COM2."""

import socket
import struct
import time

from apps import ais64_message_binary

PORT = 7799
CATALOGUE = {
    "WELCOME": ("AOS welcome", ais64_message_binary(b"Welcome from the AOS Store!")),
}


def answer(request):
    if request == b"L":
        payload = bytearray()
        for name, (description, binary) in CATALOGUE.items():
            payload += name.encode().ljust(9, b"\0")
            payload += struct.pack("<H", len(binary))
            payload += description.encode()[:30].ljust(31, b"\0")
    elif request.startswith(b"G"):
        item = CATALOGUE.get(request[1:].decode("ascii", "replace").upper())
        if item is None:
            return b"E"
        payload = item[1]
    else:
        return b"E"
    return b"K" + struct.pack("<H", len(payload)) + payload + \
        struct.pack("<H", sum(payload) & 0xffff)


def serve(port=PORT, wait=30):
    deadline = time.time() + wait
    while time.time() < deadline:
        try:
            connection = socket.create_connection(("127.0.0.1", port), 1)
            break
        except OSError:
            time.sleep(.1)
    else:
        return
    connection.settimeout(None)
    line = bytearray()
    with connection:
        while True:
            try:
                chunk = connection.recv(256)
            except OSError:
                return
            if not chunk:
                return
            for byte in chunk:
                if byte == 10:
                    if line == b"L" or (line.startswith(b"G") and
                                        line[1:].decode("ascii", "replace") in CATALOGUE):
                        try:
                            connection.sendall(answer(bytes(line)))
                        except OSError:
                            return
                    line.clear()
                elif (line.startswith(b"G") and len(line) < 9 and
                      any(name.encode().startswith(line[1:] + bytes([byte]))
                          for name in CATALOGUE)):
                    line.append(byte)
                elif byte in (ord("L"), ord("G")):
                    line.clear()
                    line.append(byte)
                elif byte != 13 and len(line) < 9:
                    line.append(byte)
