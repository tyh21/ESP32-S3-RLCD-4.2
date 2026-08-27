#!/usr/bin/env python3
import argparse
import struct
from dataclasses import dataclass


@dataclass
class Glyph:
    codepoint: int
    dwidth: int
    bbx_w: int
    bbx_h: int
    bbx_x: int
    bbx_y: int
    bitmap_rows: list[int]


def parse_bdf(path: str):
    glyphs: dict[int, Glyph] = {}
    ascent = 10
    descent = 2
    current = None
    bitmap = None

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue

            if line.startswith("FONT_ASCENT "):
                ascent = int(line.split()[1])
            elif line.startswith("FONT_DESCENT "):
                descent = int(line.split()[1])
            elif line.startswith("STARTCHAR "):
                current = {
                    "codepoint": None,
                    "dwidth": 0,
                    "bbx_w": 0,
                    "bbx_h": 0,
                    "bbx_x": 0,
                    "bbx_y": 0,
                }
                bitmap = None
            elif current is not None and line.startswith("ENCODING "):
                current["codepoint"] = int(line.split()[1])
            elif current is not None and line.startswith("DWIDTH "):
                current["dwidth"] = int(line.split()[1])
            elif current is not None and line.startswith("BBX "):
                _, w, h, x, y = line.split()
                current["bbx_w"] = int(w)
                current["bbx_h"] = int(h)
                current["bbx_x"] = int(x)
                current["bbx_y"] = int(y)
            elif current is not None and line == "BITMAP":
                bitmap = []
            elif current is not None and line == "ENDCHAR":
                codepoint = current["codepoint"]
                if codepoint is not None and codepoint >= 0:
                    glyphs[codepoint] = Glyph(
                        codepoint=codepoint,
                        dwidth=current["dwidth"],
                        bbx_w=current["bbx_w"],
                        bbx_h=current["bbx_h"],
                        bbx_x=current["bbx_x"],
                        bbx_y=current["bbx_y"],
                        bitmap_rows=bitmap or [],
                    )
                current = None
                bitmap = None
            elif bitmap is not None:
                bitmap.append(int(line, 16))

    return glyphs, ascent, descent


def render_glyph(glyph: Glyph, width: int, height: int, ascent: int, bold_x: int, bold_y: int) -> bytes:
    rows = [[0 for _ in range(width)] for _ in range(height)]

    top = ascent - glyph.bbx_y - glyph.bbx_h
    for src_y, row_bits in enumerate(glyph.bitmap_rows[: glyph.bbx_h]):
        dst_y = top + src_y
        if dst_y < 0 or dst_y >= height:
            continue

        padded_width = ((glyph.bbx_w + 7) // 8) * 8
        for src_x in range(glyph.bbx_w):
            if (row_bits & (1 << (padded_width - 1 - src_x))) == 0:
                continue
            dst_x = glyph.bbx_x + src_x
            if 0 <= dst_x < width:
                for dy in range(bold_y + 1):
                    for dx in range(bold_x + 1):
                        thick_x = dst_x + dx
                        thick_y = dst_y + dy
                        if 0 <= thick_x < width and 0 <= thick_y < height:
                            rows[thick_y][thick_x] = 1

    bytes_per_row = (width + 7) // 8
    out = bytearray(bytes_per_row * height)
    for y in range(height):
        for x in range(width):
            if rows[y][x]:
                out[y * bytes_per_row + x // 8] |= 1 << (7 - (x & 7))
    return bytes(out)


def write_fnt1(glyphs: dict[int, Glyph], output: str, width: int, height: int, ascent: int, bold_x: int, bold_y: int):
    codepoints = sorted(glyphs)
    bytes_per_row = (width + 7) // 8
    bitmap_bytes = bytes_per_row * height
    header_size = 12
    index_entry_size = 8
    bitmap_start = header_size + len(codepoints) * index_entry_size

    with open(output, "wb") as f:
        f.write(b"FNT1")
        f.write(struct.pack("<HHI", width, height, len(codepoints)))

        offset = bitmap_start
        for codepoint in codepoints:
            f.write(struct.pack("<II", codepoint, offset))
            offset += bitmap_bytes

        for codepoint in codepoints:
            f.write(render_glyph(glyphs[codepoint], width, height, ascent, bold_x, bold_y))


def main():
    parser = argparse.ArgumentParser(description="Convert a Unicode BDF bitmap font to this project's FNT1 format.")
    parser.add_argument("input", help="Input .bdf file")
    parser.add_argument("output", help="Output .bin file")
    parser.add_argument("--width", type=int, default=13, help="Output glyph cell width")
    parser.add_argument("--height", type=int, default=12, help="Output glyph cell height")
    parser.add_argument("--min-codepoint", type=lambda x: int(x, 0), default=0x20)
    parser.add_argument("--bold-x", type=int, default=0, help="Add this many pixels to the right of each set pixel")
    parser.add_argument("--bold-y", type=int, default=0, help="Add this many pixels below each set pixel")
    args = parser.parse_args()

    glyphs, ascent, descent = parse_bdf(args.input)
    glyphs = {cp: glyph for cp, glyph in glyphs.items() if cp >= args.min_codepoint}
    if not glyphs:
        raise SystemExit("No glyphs found")

    if args.height < ascent + descent:
        raise SystemExit(f"height {args.height} is smaller than BDF ascent+descent {ascent + descent}")

    write_fnt1(glyphs, args.output, args.width, args.height, ascent, args.bold_x, args.bold_y)
    print(
        f"wrote {args.output}: {len(glyphs)} glyphs, {args.width}x{args.height}, "
        f"ascent={ascent}, descent={descent}, bold_x={args.bold_x}, bold_y={args.bold_y}"
    )


if __name__ == "__main__":
    main()
