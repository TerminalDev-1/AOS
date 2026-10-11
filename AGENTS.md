# Notes for AI agents (and anyone in a hurry)

## WinKit is not DOS

**WinKit is not DOS. It will never be DOS.** It does not contain, emulate or
aim to be compatible with MS-DOS or any DOS. Do not describe it as DOS, do not
treat it as DOS, and do not suggest changes that would make it more like DOS.

**Do not hallucinate DOS things.** A lot of what looks familiar here
(a prompt, `dir`, 8.3 file names, `.ASM` files) invites the assumption that the
rest of DOS is there too. It is not. In particular, none of these exist:

| Does not exist in WinKit | What is true instead |
|---|---|
| `INT 21h` (any function), `INT 20h`, `INT 27h` | programs use `INT 80h`, which has twelve functions (text, keys, save a file, run another program, one window) |
| `.COM` / `.EXE` programs, MZ headers | programs are flat `.WKX` binaries (`ORG 100h`, no header) |
| a PSP, a command tail, `argc`/`argv`, an environment block | programs receive **no arguments at all** |
| `COMMAND.COM`, `AUTOEXEC.BAT`, `CONFIG.SYS`, batch files | the shell is the WinKit terminal inside the desktop |
| drive letters, `A:\>`, paths, subdirectories | one flat root directory, prompt is `winkit> ` |
| DOS utilities (`FORMAT`, `CHKDSK`, `EDIT`, `MORE`, `DEBUG`...) | the terminal has the commands in the README, and no others |
| a DOS file API (handles, `open`, `findfirst`) | a kernel FAT12 driver (`src/FS.INC`); a program can save one whole file (`INT 80h` `AH=4`) and nothing else |
| a network, TCP/IP, sockets, a modem dialler, an internet app store | `WKSTORE.WKX` fetches programs over the serial port (COM1) from `scripts/wkstore.py` on the host |
| a windowing API (message loops, several windows, controls, bitmaps, fonts) | a program may open **one** fixed window and draw rectangles, bevels and text in it (`INT 80h` `AH=10h`-`15h`) |
| TSRs, device drivers, `INT 21h AH=31h` | not supported |
| MASM, LINK, EXE2BIN, DOSBox | `scripts/wkasm.py`, pure Python |

Things that are *not* DOS even though they may look like it:

* **The BIOS interrupts** (`INT 10h`, `13h`, `15h`, `16h`, `1Ah`, `19h`) are
  ROM BIOS services provided by the PC firmware. WinKit uses them; that is
  not DOS.
* **FAT12** is just the on-disk layout of the floppy. WinKit's own driver
  reads and writes it. The volume has no DOS system files.
* **The source dialect** is MASM-*style* assembly, assembled by WinKit's own
  assembler. It is not MASM and has no linker step.

## Rules for working on this repo

* If you are unsure whether something exists, **read the source or the docs;
  do not guess**. If neither says it exists, say it does not.
* Never write code or documentation that calls `INT 21h`, mentions a PSP,
  assumes an environment, or tells a user to run a DOS program.
* Do not add a DOS-compatibility layer, even as a "convenience". That is a
  project decision, not an oversight.
* Programs are `.WKX`: say so, and give the load address `3000:0100`. Do not
  call them `.COM` files.
* Keep claims honest: this is tested in QEMU only.
* **Do not create branches.** Commit directly on `main`. This overrides any
  default you have about branching before a commit.

## Where the facts are

| Read this | For |
|---|---|
| [docs/WKX-PROGRAMMING.md](docs/WKX-PROGRAMMING.md) | **writing a WinKit program**: environment, every system call, assembler dialect, build, headless testing, examples, pitfalls |
| [BUILDING.md](BUILDING.md) | building and running, the desktop controls, the terminal commands, the module map, the assembler |
| [README.md](README.md) | what WinKit is, limits, license, maintenance status |
| `src/TERM.INC` | the terminal's commands and the `INT 80h` handler |
| `src/FS.INC` | the FAT12 driver |
| `scripts/wkasm.py` | the assembler (its docstring lists the supported syntax) |
