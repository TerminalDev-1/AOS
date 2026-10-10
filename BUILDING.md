# Building and running WinKit

    python scripts/mkimage.py       # assemble everything -> out/winkit.img
    python scripts/test.py          # boot it in QEMU and check every feature
    python scripts/run.py           # boot it in a QEMU window yourself
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
| `INPUT.INC`, `POWER.INC` | mouse callback, keyboard, dispatch; restart and power off |
| `progs/*.ASM` | the sample programs `HELLO`, `COUNT`, `KEYS` |

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

A program is a flat binary assembled with `ORG 100h`. It is loaded to
`3000:0100` with every segment register set to `3000h`; a `RET` or `AH=0`
returns to the terminal. Services use `INT 80h`:

| AH | Service |
|---|---|
| 0 | exit |
| 1 | print the character in AL |
| 2 | print the NUL-terminated string at DS:DX |
| 3 | wait for a key; returns AL = ASCII, AH = scan code |

Drop a new `.ASM` into `src/progs/` and rebuild; `mkimage.py` assembles every
program there and puts it on the disk.

## Tool chain

| Tool | Used for |
|---|---|
| Python 3.8+ | build, image and test scripts |
| MASM 5.10, LINK, EXE2BIN | assembling (16-bit DOS programs; see below) |
| [DOSBox-X](https://dosbox-x.com) | running those tools on the build machine |
| [QEMU](https://www.qemu.org) (`qemu-system-i386`) | booting and testing |

MASM, LINK and EXE2BIN are not in this repository. Copy `MASM.EXE`,
`LINK.EXE` and `EXE2BIN.EXE` from `v4.0/src/TOOLS` in
<https://github.com/microsoft/MS-DOS> to `./tools`, or point `WINKIT_TOOLS` at
them. DOSBox-X is found on `PATH`, in `C:\DOSBox-X`, or via `DOSBOX_X=`; QEMU
on `PATH`, in `C:\Program Files\qemu`, or via `QEMU=`.

Verified on Windows 11 with Python 3.13, DOSBox-X and QEMU. Not run on Linux
or macOS.
