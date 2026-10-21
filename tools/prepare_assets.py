"""Generate redistributable, 4-bit antialiased Vera font subsets for the D1."""
from pathlib import Path
import shutil
from PIL import Image, ImageFont, ImageDraw
import reportlab

ROOT = Path(__file__).resolve().parents[1]
font_dir = Path(reportlab.__file__).parent / 'fonts'
shutil.copyfile(font_dir / 'bitstream-vera-license.txt', ROOT / 'FONT_LICENSE.txt')
output = [
    '// Antialiased Quota Sans, derived from Bitstream Vera. See FONT_LICENSE.txt.',
    '#pragma once', '#include <Arduino.h>',
    'struct QuotaGlyph { uint32_t offset; uint8_t w,h,advance; int8_t x,y; };',
    'struct QuotaFont { const uint8_t *pixels; const QuotaGlyph *glyphs; };',
]
for size, bold in ((13, False), (14, False), (16, False), (18, True),
                   (24, True), (28, True), (32, False), (100, False)):
    scale = 3
    font = ImageFont.truetype(str(font_dir / ('VeraBd.ttf' if bold else 'Vera.ttf')), size)
    large_font = ImageFont.truetype(str(font_dir / ('VeraBd.ttf' if bold else 'Vera.ttf')), size * scale)
    name = f'QuotaSans{size}'
    pixels, glyphs = [], []
    for code in range(32, 127):
        char = chr(code)
        x0, y0, x1, y1 = font.getbbox(char, anchor='ls')
        if size == 100 and char not in '0123456789-':
            x0 = y0 = x1 = y1 = 0
        w, h = x1 - x0, y1 - y0
        offset = len(pixels)
        image = Image.new('L', (max(1, w) * scale, max(1, h) * scale))
        ImageDraw.Draw(image).text((-x0 * scale, -y0 * scale), char,
                                  font=large_font, fill=255, anchor='ls')
        image = image.resize((max(1, w), max(1, h)), Image.Resampling.LANCZOS)
        coverage = [round(image.getpixel((x, y)) * 15 / 255) for y in range(h) for x in range(w)]
        for i in range(0, len(coverage), 2):
            pixels.append((coverage[i] << 4) | (coverage[i + 1] if i + 1 < len(coverage) else 0))
        glyphs.append((offset, w, h, round(font.getlength(char)), x0, y0))
    output += [f'const uint8_t {name}Pixels[] PROGMEM = {{',
               ','.join(map(str, pixels)), '};',
               f'const QuotaGlyph {name}Glyphs[] PROGMEM = {{']
    output += ['{' + ','.join(map(str, glyph)) + '},' for glyph in glyphs]
    output += ['};', f'const QuotaFont {name} = {{{name}Pixels, {name}Glyphs}};']
(ROOT / 'firmware/D1_AI_Quota/src/quota_fonts.h').write_text('\n'.join(output) + '\n', encoding='utf-8')
print('Prepared eight antialiased font subsets; Bitstream Vera licence included.')
