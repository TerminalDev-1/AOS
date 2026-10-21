# Building and running AOS

Install Rust, Python 3.10 or newer, QEMU x86-64 with EDK2 firmware, and the
Python requirements:

```text
rustup target add x86_64-unknown-none x86_64-unknown-uefi
python -m pip install -r aos/requirements-iso.txt
```

From the repository root, build and test:

```text
python aos/build.py
python aos/test.py
python aos/test_iso.py
```

Every full build creates a fresh `aos/out/aos.iso`. The build generates its
FAT12 data volume directly, compiles the kernel and UEFI loader, and packages
the loader and seed in a UEFI El Torito ISO. The build does not use 16-bit
source or a BIOS disk image.

Boot the live ISO in QEMU with `python aos/run.py`. Add `--store` to serve
native applications over the guest's second serial port. Live saves go to a
RAM-backed data volume and disappear on shutdown. Use
`python aos/run.py --installed --store` for a combined EFI/data disk with
persistent files. `python aos/install.py` refreshes that image while preserving
its data partition. `python aos/run.py --folder` retains a virtual EFI folder
path for development, and `--skip-build` uses existing binaries.

On systems with nonstandard locations, set `QEMU_X64`, `AOS_UEFI_CODE` and
`AOS_UEFI_VARS`. These are paths to QEMU and its EDK2 code and variable images.

The full-width bottom taskbar contains a Start button, app search, pinned
application buttons and an RTC clock/date. Use the Windows key, Ctrl+Esc,
the Start button or the search field to open Start. Type an application name
to filter the list, use Up/Down and Enter to launch it, or click an app or
pinned tile. Esc or an outside click dismisses Start. Its Power entry opens
the shutdown confirmation; Enter confirms it. The shell keeps AOS's rounded
light panels, smooth purple/blue wallpaper and red AOS branding.

Use Left/Right to select a taskbar app and Enter to open it. Clicking its
active taskbar button minimizes the window; clicking again restores it.
Files and Programs
use Up/Down. Esc returns to Files. Files can browse the FAT12 root and the
example `DOCS` folder. Terminal currently supports `help`, `dir`, `type NAME`,
`ver` and `mem`. See [AIS programming](docs/AIS-PROGRAMMING.md) for the native
program format.

Window title bars have minimize, maximize/restore and close buttons at the
right. Maximize fills the workspace between the AOS header and taskbar. Minimize
and close hide the app window; click its taskbar button or select it and press
Enter to reopen it. Editor buffers stay in memory. The UEFI splash displays
a large red AOS logo across the screen before the kernel is loaded.
