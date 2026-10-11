# Building and running WinKit

    python scripts/mkimage.py       # assemble everything -> out/winkit.img
    python scripts/test.py          # boot it in QEMU and check every feature
    python scripts/test_wkasm.py    # self-test of the assembler
    python scripts/run.py           # boot it in a QEMU window yourself
    python scripts/run.py --store   # the same, with the WinKit Store reachable
    python scripts/showcase.py      # screenshots + overview in out/screens/

Add `--snapshot` to `run.py` to discard disk writes when QEMU exits.

## The widescreen display driver

Under QEMU the desktop is **848 x 480** (16:9), so it fills a widescreen
emulator window instead of sitting in a 4:3 rectangle. That is WinKit's
own doing, in [`src/VIDEO.INC`](src/VIDEO.INC), not an emulator option:

* WinKit still uses VGA mode 12h (16 colours, four bit planes). A real
  monitor needs that mode's exact timing, but QEMU has no monitor and
  takes the picture size from the VGA registers. The driver changes two
  CRT controller registers after the mode is set: the number of displayed
  columns (106 instead of 80) and the length of a row in video memory.
* It does this only when it recognises QEMU's video card by its "Bochs
  VBE" ID register (ports 1CEh/1CFh). On any other card nothing is
  touched and the screen stays 640 x 480; `python scripts/run.py` with
  `-vga cirrus` in QEMU shows that fallback.
* The height stays 480, so 848 x 480 is 50,880 bytes a plane and still
  fits a VGA plane. QEMU's own 16-colour VBE modes are not used: a guest
  cannot draw in them, because QEMU ignores the plane-select register
  while VBE is on.
* The screen size is not a constant in the kernel: `scrw`, `rowb` and the
  values derived from them (`DATA.INC`) are set at start-up. The welcome
  and Shut Down dialogs and a program's window are centred; the tray
  clock follows the right edge; windows can be dragged across the full
  width. A program's client area is unchanged (410 x 264).

`run.py` also asks QEMU to scale the screen to its window
(`-display gtk,zoom-to-fit=on`), so a maximized window shows a large
desktop. `--fullscreen` is there if you want the whole monitor
(Ctrl+Alt+F leaves it, Ctrl+Alt+G releases the mouse).

## Installing on a hard disk

    python scripts/run.py --hda out/disk.img           # floppy + an empty hard disk
    python scripts/run.py --hda out/disk.img --boot c  # afterwards: hard disk only

`--hda` attaches a raw hard disk image and creates it (empty, 16 MB) if the
file does not exist. In WinKit, open Start > Programs > SETUP (or type
`setup` in the terminal) and press Install twice: once to ask, once to
confirm. Setup (`src/progs/SETUP.ASM`) copies the disk WinKit was started
from, sector by sector, to the first hard disk and writes that disk's
geometry into the boot sector. It takes a few seconds. When it is done the
Install button becomes **Restart**: press it and the machine starts from
the hard disk at once, floppy still in the drive (Setup loads the hard
disk's boot sector itself, so the BIOS boot order does not matter). The
hard disk has everything that was on the floppy at the time, installed
programs included. On later runs use `--boot c`, which leaves the floppy
out of the machine; the ordinary Restart in Shut Down follows the BIOS boot
order, so with the floppy in it starts the floppy again.

* **Everything on the hard disk is erased.** Setup writes from the first
  sector on and makes no partition table, so the whole disk becomes the
  WinKit disk and nothing else can share it.
* The volume on the hard disk is still the 1.44 MB FAT12 volume; the rest
  of the disk is not used.
* Setup refuses when there is no hard disk, when it is smaller than
  1.44 MB, or when WinKit is already running from the hard disk.
* Like everything here this is tested in QEMU only. Do not point it at a
  real disk you care about.

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
| `VIDEO.INC` | the QEMU display driver: widens mode 12h to 848 x 480 |
| `WIDGETS.INC` | 3D bevels, title gradient, window chrome, icons, table-driven buttons |
| `SCENES.INC` | desktop and icons, taskbar, Start menu, welcome and Shut Down dialogs |
| `WINDOWS.INC` | Files / Notes / System bodies, minimize, dragging |
| `FS.INC` | FAT12 driver over BIOS INT 13h: read, write, delete, rename |
| `STORAGE.INC` | the Files list and `NOTES.TXT` |
| `TERM.INC` | the WinKit terminal, its commands, and the INT 80h program interface |
| `PGUI.INC` | the INT 80h window calls: a program's window, drawing, key and mouse events |
| `INPUT.INC`, `POWER.INC` | mouse callback, keyboard, dispatch; restart and power off |
| `progs/*.ASM` | programs on the disk: `HELLO`, `HI`, `COUNT`, `KEYS`, `FIB`, `REVERSE`, `WINDOW`, the Store `WKSTORE`, and the hard disk installer `SETUP` |

The desktop uses VGA mode 12h (640x480, or 848x480 under QEMU) with its own
16-colour palette and
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
| Start > Programs | Lists the `.WKX` programs on the disk (up to 16); click one to run it |
| Desktop shortcuts | Double-click to run: `SETUP`, and every program that was not shipped with WinKit (up to 15, in columns beside the system icons) |
| Mouse | Start menu, taskbar buttons, window focus, title-bar dragging, minimize and close buttons, double-click a desktop icon, click a `.WKX` in Files to run it |
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
string, read key, save file, run another program, and six calls that give
the program a window to draw in with key and mouse events. Drop a new `.ASM` into `src/progs/` and rebuild;
`mkimage.py` assembles every program there and puts it on the disk.

The full reference, with the memory map, the assembler dialect, testing
recipes and examples, is [docs/WKX-PROGRAMMING.md](docs/WKX-PROGRAMMING.md).

## The WinKit Store

The Store is a window that lists programs, installs the one you pick as
`NAME.WKX` on the disk, and runs it. It is an ordinary native program, `WKSTORE.WKX`,
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
| Run button, Space | Leave the Store and start the selected program (it must be installed) |
| Reload button, R | Ask the server for the catalogue again |
| `<` / `>` buttons, PgUp / PgDn | Page through the list (10 per page) |
| Close button, Esc or Q | Leave the Store |

While the Store is open it is the only thing running: like every WinKit
program it has the machine to itself, so the other windows do not respond
until it is closed. An installed program stays on the disk: start it any
time from its **desktop shortcut**, from **Start > Programs**, by clicking
it in Files (press Refresh if it is not listed yet), or by name in the
terminal. Run in the Store starts
it straight away.

**The Store only carries windowed programs.** The server leaves out any
source that never opens a window (it looks for `mov ah,10h` / `int 80h` in
the assembled program) and says so in its log. The three programs in
`store/` (`CLOCK`, `DICE`, `GUESS`) are windowed.

The server offers every `.ASM` in [`store/`](store/): it assembles each one
with wkasm and takes the description from a `; Store: text` comment line (up
to 30 characters). To publish a program, drop `NAME.ASM` there and press
Reload in the Store; nothing needs rebuilding. `python scripts/wkstore.py --list`
prints the catalogue. The store holds at most 64 programs of up to 52 KB.
Without `--store`, the Store waits three seconds and says the server did not
answer. The wire protocol is described at the top of `wkstore.py`.

### Which programs get a desktop shortcut

The files `mkimage.py` puts on the disk carry the FAT "system" attribute,
except `SETUP.WKX`. The desktop shows a shortcut for every `.WKX` *without*
that attribute: Setup, plus anything saved later, which is what the Store
installs (and what `copy` makes in the terminal). Deleting the file removes
the shortcut. Nothing else is stored, so shortcuts survive a restart and
come along when Setup copies the system to a hard disk.

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
