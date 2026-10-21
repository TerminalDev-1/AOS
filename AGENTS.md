# AOS repository notes

This repository contains one operating system: **AOS**, a 64-bit x86-64 UEFI
system. The former 16-bit BIOS edition has been removed. Do not describe the
current system as DOS or claim that it runs old 8086 `.AIS` binaries.

The source lives in `aos/`. The UEFI loader embeds a native FAT12 seed, exits
boot services and starts the freestanding Rust kernel. The kernel draws its
desktop to the UEFI graphics framebuffer, handles timer, keyboard and mouse
input, and offers Files, Notes, Write, Terminal, Programs and Store. Versioned
x86-64 `.AIS` applications run in ring 3; see `docs/AIS-PROGRAMMING.md` for the
actual format and small syscall interface. The system has been tested in QEMU
with EDK2, not on real hardware.

## Required build workflow

1. **Every AOS build must generate a fresh ISO.** From the repository root,
   run `python aos/build.py`. This creates the FAT12 seed, compiles the kernel
   and UEFI loader, and writes `aos/out/aos.iso`. A direct `cargo build` checks
   only a component and does not finish a system build.
2. Run `python aos/test.py` and `python aos/test_iso.py` before calling a build
   working. The first boots an installed disk in QEMU; the second boots the ISO
   without a writable disk and checks the live RAM volume, taskbar, Start menu,
   window controls, cursor and full-screen boot logo.
3. Use `python aos/run.py` to boot the ISO interactively. Add `--store` to run
   the host-side native application catalogue or `--installed` for persistent
   disk data. Update the docs when behavior changes.

The owner designated this as an experimental copy and authorized breaking
changes. Keep claims tied to source or tests. Do not push to any published
repository without a request.

The public AOS repository is `TerminalDev-1/AOS`. Its Git remote is `origin`;
the original WinKit repository is retained as `upstream`. Publish AOS work to
the AOS repository rather than the historical WinKit project.

The current direction is to keep Rust and AOS's own visual identity: a smooth
purple/blue wallpaper, rounded light windows, an AOS-branded Start menu and a
light bottom taskbar. Do not replace this with Windows wallpaper or branding.
The owner's language-choice clarification is
recorded in `How-developer-felt-when-he-discovered-that-operating-systems-could-be-built-in-other-languages-not-mainly-assembly.md`.
