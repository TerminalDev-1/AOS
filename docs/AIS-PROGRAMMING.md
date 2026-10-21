# AIS application format

AOS uses a versioned `.AIS` file for native x86-64 programs. Its current
header begins with `AIS64` to identify the architecture and ABI version.
The v1 format and ABI are experimental; it currently supports one small app
at a time in QEMU.

| Offset | Size | Meaning |
|---|---:|---|
| 0 | 8 | Bytes `AIS64\0\x01\0` (magic and version) |
| 8 | 4 | Little-endian code length |
| 12 | 4 | Little-endian entry offset within code |
| 16 | length | Position-independent x86-64 code and data |

The complete file must fit in 4096 bytes. The kernel maps the code at virtual
address `0x20000000` as user-readable/executable and a separate 4096-byte
user stack at `0x20001000` under a page-table root separate from the kernel's.
The entry starts with no arguments. It should
finish through `INT 80h`, `RAX=0`, rather than returning. The kernel maps all
its own pages supervisor-only. A user fault or a 300-tick time limit returns
control to Programs.

System calls use the System V x86-64 register convention where applicable:

| RAX | Operation | Inputs |
|---:|---|---|
| 0 | Exit | none |
| 1 | Write to the Programs output panel | RDI = pointer into the app's code or stack page; RSI = byte count, capped at 256 total output bytes |

The [demo builder](../aos/apps.py) emits `HELLO.AIS`, which loads from the
FAT12 disk and calls this ABI. `aos/store.py` serves another native app over
the second serial port; `python aos/run.py --store` enables it.

This v1 interface does not yet offer file or window syscalls, arguments,
dynamic memory, multiple pages, or concurrent processes. AOS Write, Files,
the dock and the Store interface still live in the kernel. These are current
limits of the implementation, not promises of forward compatibility.
