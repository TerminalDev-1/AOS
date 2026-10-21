"""Build and boot the AOS UEFI image in QEMU."""

import argparse
import os
import shutil
import subprocess
import threading
from pathlib import Path

from apps import install_demo_folder, install_hello
from seed import build_seed
from store import PORT as STORE_PORT, serve as serve_store

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "out"
ESP = OUT / "esp"
QEMU_SHARE = Path(r"C:\Program Files\qemu\share")


def find_qemu():
    choices = [os.environ.get("QEMU_X64"), shutil.which("qemu-system-x86_64"),
               r"C:\Program Files\qemu\qemu-system-x86_64.exe"]
    for choice in choices:
        if choice and Path(choice).is_file():
            return str(choice)
    raise SystemExit("qemu-system-x86_64 not found; set QEMU_X64")


def prepare(build=True):
    code = Path(os.environ.get("AOS_UEFI_CODE", QEMU_SHARE / "edk2-x86_64-code.fd"))
    vars_template = Path(os.environ.get("AOS_UEFI_VARS", QEMU_SHARE / "edk2-i386-vars.fd"))
    if not code.is_file() or not vars_template.is_file():
        raise SystemExit("QEMU x86-64 UEFI firmware missing; set AOS_UEFI_CODE and AOS_UEFI_VARS")
    OUT.mkdir(parents=True, exist_ok=True)
    seed_image = OUT / "seed.img"
    build_seed(seed_image)
    install_hello(seed_image)
    install_demo_folder(seed_image)
    data_image = OUT / "data.img"
    if not data_image.exists():
        shutil.copy2(seed_image, data_image)
    install_hello(data_image, OUT / "HELLO.AIS")
    install_demo_folder(data_image)
    if build:
        subprocess.run(["cargo", "build", "--release", "--target", "x86_64-unknown-none"],
                       cwd=ROOT / "kernel", check=True)
        shutil.copy2(ROOT / "kernel" / "target" / "x86_64-unknown-none" / "release" / "kernel",
                     OUT / "kernel.elf")
        subprocess.run(["cargo", "build", "--release", "--target", "x86_64-unknown-uefi"],
                       cwd=ROOT, check=True)
    binary = ROOT / "target" / "x86_64-unknown-uefi" / "release" / "aos.efi"
    boot = ESP / "EFI" / "BOOT"
    boot.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, boot / "BOOTX64.EFI")
    if build:
        from make_iso import build_iso
        build_iso(boot / "BOOTX64.EFI", seed_image, OUT / "aos.iso")
    vars_image = OUT / "uefi-vars.fd"
    shutil.copy2(vars_template, vars_image)
    return code, vars_image


def media_args(code, vars_image, data_image=None, boot_image=None, integrated=False):
    if data_image is None:
        data_image = OUT / "data.img"
    args = [
        "-drive", f"if=pflash,unit=0,format=raw,readonly=on,file={code.as_posix()}",
        "-drive", f"if=pflash,unit=1,format=raw,file={vars_image.as_posix()}",
    ]
    if integrated:
        if boot_image is None:
            raise ValueError("integrated boot requires an image")
        args += ["-drive", f"if=ide,index=0,format=raw,file={boot_image.as_posix()}"]
        return args
    args += ["-drive", f"if=ide,index=0,format=raw,file={data_image.as_posix()}", "-usb"]
    if boot_image is None:
        args += ["-drive", f"if=none,id=esp,format=raw,file=fat:rw:{ESP.as_posix()}"]
    else:
        args += ["-drive", f"if=none,id=esp,format=raw,file={boot_image.as_posix()}"]
    args += ["-device", "usb-storage,drive=esp,bootindex=0"]
    return args


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--store", action="store_true",
                        help="serve native AIS64 apps over the second serial port")
    boot_mode = parser.add_mutually_exclusive_group()
    boot_mode.add_argument("--installed", action="store_true",
                           help="boot the writable installed disk image")
    boot_mode.add_argument("--iso", action="store_true",
                           help="boot the read-only ISO with a temporary RAM data disk")
    boot_mode.add_argument("--folder", action="store_true",
                           help="use QEMU's virtual EFI folder for development")
    args = parser.parse_args()
    args.iso = args.iso or (not args.installed and not args.folder)
    code, vars_image = prepare(build=not args.skip_build)
    serial = OUT / "serial.log"
    cmd = [find_qemu(), "-machine", "pc", "-cpu", "qemu64", "-m", "256",
           "-vga", "std", "-net", "none", "-boot", "d" if args.iso else "c",
           "-display", "gtk,zoom-to-fit=on"]
    boot_image = None
    if args.installed:
        from install import DATA_PARTITION_START, build_boot_image
        boot_image = build_boot_image(ESP / "EFI" / "BOOT" / "BOOTX64.EFI",
                                      OUT / "aos-boot.img", OUT / "data.img",
                                      preserve_data=True)
        install_demo_folder(boot_image, DATA_PARTITION_START * 512)
    if args.iso:
        from make_iso import build_iso
        iso_path = OUT / "aos.iso"
        if args.skip_build or not iso_path.is_file():
            build_iso(ESP / "EFI" / "BOOT" / "BOOTX64.EFI",
                      OUT / "seed.img", iso_path)
        cmd += ["-drive", f"if=pflash,unit=0,format=raw,readonly=on,file={code.as_posix()}",
                "-drive", f"if=pflash,unit=1,format=raw,file={vars_image.as_posix()}",
                "-cdrom", str(iso_path)]
    else:
        cmd += media_args(code, vars_image, boot_image=boot_image,
                          integrated=args.installed)
    cmd += ["-serial", f"file:{serial.as_posix()}"]
    if args.store:
        cmd += ["-serial", f"tcp:127.0.0.1:{STORE_PORT},server=on,wait=off"]
        threading.Thread(target=serve_store, daemon=True).start()
    print(f"[aos] booting UEFI image; serial log: {serial}", flush=True)
    subprocess.call(cmd)


if __name__ == "__main__":
    main()
