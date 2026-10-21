# AOS source

This directory contains the sole AOS build: a 64-bit UEFI loader, freestanding
kernel, native FAT12 seed generator, ISO builder, installed-disk builder and
QEMU integration tests. Start with the [root README](../README.md) and
[building guide](../BUILDING.md).

`python aos/build.py` is the canonical build and always creates
[`out/aos.iso`](out/aos.iso). `python aos/test.py` checks installed-disk boot and
desktop behavior; `python aos/test_iso.py` checks live ISO boot, saves, Start
search and pinned apps, taskbar stability, window controls and cursor cleanup.
`python aos/run.py` opens the live ISO in QEMU by default.
