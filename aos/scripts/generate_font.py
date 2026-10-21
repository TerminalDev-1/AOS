"""Rasterize the OFL-licensed Inter font for AOS's no-std renderer.

Requires Pillow only when regenerating assets; the checked-in atlases are used
directly by the kernel build. Run from the repository root with:

    python aos/scripts/generate_font.py
"""

from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont

ASSETS = Path(__file__).resolve().parents[1] / "kernel" / "assets"
FONT = ASSETS / "Inter.ttf"


def render(name, size, cell_width, cell_height, baseline):
    font = ImageFont.truetype(FONT, size)
    try:
        font.set_variation_by_axes([14, 450])
    except (AttributeError, OSError, ValueError):
        pass
    atlas = bytearray()
    advances = bytearray()
    preview = Image.new("L", (cell_width * 16, cell_height * 6), 245)
    for index, codepoint in enumerate(range(32, 127)):
        glyph = Image.new("L", (cell_width, cell_height))
        ImageDraw.Draw(glyph).text((1, baseline), chr(codepoint), font=font,
                                   fill=255, anchor="ls")
        atlas.extend(glyph.tobytes())
        advances.append(max(4, min(cell_width, round(font.getlength(chr(codepoint))))))
        preview.paste(glyph, ((index % 16) * cell_width,
                              (index // 16) * cell_height))
    (ASSETS / f"{name}.bin").write_bytes(atlas)
    (ASSETS / f"{name}-advance.bin").write_bytes(advances)
    preview.save(ASSETS / f"{name}-preview.png")


if __name__ == "__main__":
    render("inter-small", 13, 16, 18, 14)
    render("inter-large", 20, 24, 28, 22)
    font = ImageFont.truetype(FONT, 512)
    try:
        font.set_variation_by_axes([14, 600])
    except (AttributeError, OSError, ValueError):
        pass
    left, top, right, bottom = font.getbbox("AOS")
    logo = Image.new("L", (right - left + 4, bottom - top + 4))
    ImageDraw.Draw(logo).text((2 - left, 2 - top), "AOS", font=font, fill=255)
    (ASSETS / "boot-logo.bin").write_bytes(struct.pack("<HH", *logo.size) + logo.tobytes())
    preview = Image.new("RGB", logo.size, (13, 18, 30))
    preview.paste((235, 52, 65), mask=logo)
    preview.save(ASSETS / "boot-logo-preview.png")
