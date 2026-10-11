# Building and running WinKit

    python scripts/mkimage.py       # assemble everything -> out/winkit.img
    python scripts/test.py          # boot it in QEMU and check every feature
    python scripts/test_wkasm.py    # self-test of the assembler
    python scripts/run.py           # boot it in a QEMU window yourself
    python scripts/run.py --store   # the same, with the WinKit Store reachable
    python scripts/showcase.py      # screenshots + overview in out/screens/

Add `--snapshot` to `run.py` to discard disk writes when QEMU exits.

## What is on the disk

| Where | What |
|---|---|
| sector 0 | [`BOOT.ASM`](src/BOOT.ASM): FAT12 BPB and a loader that reads the kernel |
| sectors 1..K | the kernel (`GUI.ASM` and its modules), stored as FAT "reserved" sectors |
| FAT12 volume | `README.TXT`, the `.WKX` programs, and whatever you create |

The boot sector loads the kernel to `1000:0100` and jumps to it; the kernel
reads the volume parameters from the boot sector and caches the FAT and root
directory in a second segment.

## The source

Everything is under [`src/`](src/). `GUI.ASM` is the entry point and main
loop and `INCLUDE`s the rest.

| Module | Role |
|---|---|
| `BOOT.ASM` | boot sector |
| `MACROS.INC`, `CONST.INC` | long jumps and drawing macros; palette, layout, action codes |
| `DATA.INC` | state, button tables, strings, palette, pointer bitmaps |
| `GFX.INC` | clipped rectangles, pixel-accurate text, pointer, window blit |
| `WIDGETS.INC` | 3D bevels, title gradient, window chrome, icons, table-driven buttons |
| `SCENES.INC` | desktop and icons, taskbar, Start menu, welcome and Shut Down dialogs |
| `WINDOWS.INC` | Files / Notes / System bodies, minimize, dragging |
| `FS.INC` | FAT12 driver over BIOS INT 13h: read, write, delete, rename |
| `STORAGE.INC` | the Files list and `NOTES.TXT` |
| `TERM.INC` | the WinKit terminal, its commands, and the INT 80h program interface |
| `PGUI.INC` | the INT 80h window calls: a program's window, drawing, key and mouse events |
| `INPUT.INC`, `POWER.INC` | mouse callback, keyboard, dispatch; restart and power off |
| `progs/*.ASM` | programs on the disk: `HELLO`, `HI`, `COUNT`, `KEYS`, `FIB`, `REVERSE`, `WINDOW`, and the Store, `WKSTORE` |

The desktop uses VGA mode 12h (640x480) with its own 16-colour palette and
writes straight to planar video memory. PS/2 mouse input comes from the BIOS
callback; the clock is the BIOS real-time clock. Only 8086 instructions are
used.

Plain pointer motion only swaps the arrow using pixels saved underneath it;
dragging a window copies it with the VGA latches and repaints just the strips
it uncovered; typing in WinKit redraws only the terminal.

## The desktop

| Control | Action |
|---|---|
| Enter on the welcome dialog | Enter the desktop |
| Mouse | Start menu, taskbar buttons, window focus, title-bar dragging, minimize and close buttons, double-click a desktop icon |
| Arrow keys + Space | Move the pointer and activate its target without a mouse |
| F1 / F2 / F3 / F6 | Open Files / Notes / System / WinKit |
| F7 | Run the Store (`WKSTORE.WKX`) |
| F4 | Save Notes to `NOTES.TXT` |
| F5 | Close the active window |
| F9 | Open or close the Start menu (Esc closes it) |
| PgUp / PgDn, R | Page and refresh the Files list |
| F10 | Shut Down |
| R / P, Enter, Esc in Shut Down | Restart / Power off, OK on the chosen option, Cancel |

## The WinKit terminal

Open it from the Start menu or with F6.

| Command | Does |
|---|---|
| `help` | list the commands |
| `dir` | list the files with sizes and free space |
| `type FILE` | show a text file |
| `echo TEXT` / `echo TEXT > FILE` | print text / write a file |
| `copy A B`, `ren A B`, `del FILE` | copy, rename, delete |
| `cls`, `ver`, `time`, `mem`, `reboot`, `exit` | housekeeping |
| `NAME` or `NAME.WKX` | run a native program |

### Native programs (`.WKX`)

A program is a flat binary assembled with `ORG 100h`, loaded to
`3000:0100`, using the `INT 80h` services: exit, print character, print
string, read key, save file, and five calls that give the program a window
to draw in with key and mouse events. Drop a new `.ASM` into `src/progs/` and rebuild;
`mkimage.py` assembles every program there and puts it on the disk.

The full reference, with the memory map, the assembler dialect, testing
recipes and examples, is [docs/WKX-PROGRAMMING.md](docs/WKX-PROGRAMMING.md).

## The WinKit Store

The Store is a window that lists programs and installs the one you pick as
`NAME.WKX` on the disk. It is an ordinary native program, `WKSTORE.WKX`,
that draws its window through `INT 80h`. WinKit has no network: the
program talks to the serial port (COM1, polled), and the other end of that
port is the store server, [`scripts/wkstore.py`](scripts/wkstore.py),
running on the host.

    python scripts/run.py --store    # QEMU with COM1 wired to the server

Open it from Start > Store, by double-clicking the Store icon, with F7, or
by typing `wkstore` in the terminal.

| Control | Action |
|---|---|
| Click a row, Up / Down | Select a program |
| Install button, Enter or I | Download the selected program and save it |
| Reload button, R | Ask the server for the catalogue again |
| Prev / Next buttons, PgUp / PgDn | Page through the list (10 per page) |
| Close button, Esc or Q | Leave the Store |

While the Store is open it is the only thing running: like every WinKit
program it has the machine to itself, so the other windows do not respond
until it is closed. Installed programs are run from the terminal by name.

The server offers every `.ASM` in [`store/`](store/): it assembles each one
with wkasm and takes the description from a `; Store: text` comment line (up
to 30 characters). To publish a program, drop `NAME.ASM` there and press
Reload in the Store; nothing needs rebuilding. `python scripts/wkstore.py --list`
prints the catalogue. The store holds at most 64 programs of up to 52 KB.
Without `--store`, the Store waits three seconds and says the server did not
answer. The wire protocol is described at the top of `wkstore.py`.

## The assembler

[`scripts/wkasm.py`](scripts/wkasm.py) is WinKit's own 8086 assembler: one
Python file with no dependencies. It reads the MASM-style dialect the sources
are written in (macros, `INCLUDE`, `EQU`, `PROC`, `DUP`, expressions, typed
variables) and writes flat binaries. It picks the same instruction encodings
as MASM 5.10 does, and the kernel, boot sector and every sample program were
verified to be **byte-identical** to MASM's output when it was written.
`scripts/test_wkasm.py` checks known encodings and error handling.

## Requirements

| Tool | Used for |
|---|---|
| Python 3.8+ | assembler, image builder and test scripts |
| [QEMU](https://www.qemu.org) (`qemu-system-i386`) | booting and testing |

QEMU is found on `PATH`, in `C:\Program Files\qemu`, or via `QEMU=`.
Verified on Windows 11 with Python 3.13 and QEMU. Not run on Linux or macOS.
