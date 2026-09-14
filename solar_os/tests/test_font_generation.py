import importlib.util
from pathlib import Path
import sys
import unittest

from PIL import ImageFont


REPOSITORY = Path(__file__).resolve().parents[1]
GENERATOR_PATH = REPOSITORY / "fonts" / "generate_u8g2_fonts.py"
SPEC = importlib.util.spec_from_file_location("generate_u8g2_fonts", GENERATOR_PATH)
assert SPEC is not None and SPEC.loader is not None
generate_u8g2_fonts = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = generate_u8g2_fonts
SPEC.loader.exec_module(generate_u8g2_fonts)


class FontGenerationTest(unittest.TestCase):
    def test_italic_overhang_is_preserved_without_widening_advance(self):
        for filename in ("Italic.ttf", "BoldItalic.ttf"):
            for size in generate_u8g2_fonts.DEFAULT_SIZES:
                with self.subTest(filename=filename, size=size):
                    font = ImageFont.truetype(str(REPOSITORY / "fonts" / filename), size)
                    cell_width = generate_u8g2_fonts.font_cell_width(font, [ord("d")], True)
                    ascent, descent = font.getmetrics()

                    glyph = generate_u8g2_fonts.rasterize_glyph(
                        font,
                        ord("d"),
                        cell_width,
                        ascent + descent,
                        ascent,
                        128,
                        False,
                    )

                    self.assertEqual(glyph.x_offset, 0)
                    self.assertGreater(glyph.width, cell_width)
                    overhang_width = glyph.width - cell_width
                    self.assertTrue(any(row & ((1 << overhang_width) - 1) for row in glyph.rows))


if __name__ == "__main__":
    unittest.main()
