"""Canonical AOS build: native data volume, kernel, UEFI loader, and ISO."""

from run import OUT, prepare


def main():
    prepare(build=True)
    iso = OUT / "aos.iso"
    if not iso.is_file():
        raise SystemExit("AOS build failed: ISO was not generated")
    print(f"[aos] build complete: {iso} ({iso.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
