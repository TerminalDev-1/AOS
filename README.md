# WinKit

A small standalone operating system written in 16-bit x86 assembly: a
Windows 98-style desktop and its own terminal, **WinKit**. It boots from its
own boot sector. There is no DOS underneath; disk access goes through the BIOS
and the kernel's own FAT12 driver.

![WinKit: desktop, files, terminal and native programs](docs/overview.png)

> ## Maintenance status
>
> **I am not maintaining this project.** I update it whenever I feel like it.
> There may be the occasional minor update, but when an update does come it
> will usually be a substantial one. There is no schedule and no promise of
> support.

## What it has

- **Desktop:** teal wallpaper, silver 3D-bevelled windows with gradient title
  bars, Start menu, taskbar with window buttons and a clock, desktop icons,
  minimize and close, flicker-free window dragging.
- **Apps:** Files (lists the disk), Notes (a text editor that saves to disk),
  System Properties, and the WinKit terminal.
- **WinKit terminal:** `help dir type echo copy ren del cls ver time mem
  reboot exit`, `echo TEXT > FILE`, and it runs native `.WKX` programs.
- **FAT12 driver:** read, write, delete and rename on a 1.44 MB floppy image,
  through BIOS INT 13h.
- **Native programs:** flat binaries that talk to the system through
  `INT 80h` (exit, print a character, print a string, wait for a key).
  Examples are in [`src/progs`](src/progs).

## Try it

You need Python 3.8+, [QEMU](https://www.qemu.org) and
[DOSBox-X](https://dosbox-x.com) (only to run the 16-bit assembler on the
build machine). Put `MASM.EXE`, `LINK.EXE` and `EXE2BIN.EXE` in `./tools`, or
set `WINKIT_TOOLS` to the folder that has them; they come from Microsoft's
[MS-DOS repository](https://github.com/microsoft/MS-DOS) under
`v4.0/src/TOOLS`.

    python scripts/mkimage.py        # assemble everything -> out/winkit.img
    python scripts/run.py            # boot it in a QEMU window
    python scripts/test.py           # boot it headless and check every feature
    python scripts/showcase.py       # screenshots of every feature

See [BUILDING.md](BUILDING.md) for the controls, the terminal, the program
interface and how the code is organised.

## Limits

- One-level volume (no folders); files up to 52 KB; Notes holds 240 characters.
- WinKit programs cannot be interrupted; a program that never exits needs a
  reset.
- Existing DOS programs do not run: they need DOS's INT 21h services, which
  WinKit deliberately does not provide.
- Tested in QEMU only, not on physical hardware.

## License

Copyright (C) 2026 TerminalDev-1.

WinKit is free software, released under the **GNU General Public License,
version 3**. See [LICENSE](LICENSE). It comes with no warranty.

The assembler and linker used to build it are Microsoft's 1988 tools from the
MIT-licensed MS-DOS release; they are not part of this repository and none of
Microsoft's code is included in WinKit.
