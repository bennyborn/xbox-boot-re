"""Puts the scanned-out images of several runs side by side, one row per run.

    python tools/xemu/contact_sheet.py c3_ko_base c3_ko_57c c3_ko_968 c3_ko_b5c c3_ko_17ec \
        --out out/xemu/c3_knockout_sheet.png

Takes sequence/*.png of each run (bench.py --frames), in file order, scaled
to 256x192, with the run name on the left and the file name on each image.
"""

import argparse
import os

from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
W, H, LEFT = 256, 192, 90


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('runs', nargs='+')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    rows = []
    for r in a.runs:
        d = os.path.join(ROOT, 'out', 'xemu', 'runs', r, 'sequence')
        rows.append((r, [os.path.join(d, f) for f in sorted(os.listdir(d))]))
    cols = max(len(f) for _, f in rows)
    sheet = Image.new('RGB', (LEFT + cols * W, len(rows) * H), 'white')
    draw = ImageDraw.Draw(sheet)
    for y, (name, files) in enumerate(rows):
        draw.text((4, y * H + H // 2), name, fill='black')
        for x, f in enumerate(files):
            sheet.paste(Image.open(f).convert('RGB').resize((W, H)), (LEFT + x * W, y * H))
            draw.text((LEFT + x * W + 4, y * H + 4), os.path.basename(f), fill='yellow')
    sheet.save(a.out)
    print(a.out)


if __name__ == '__main__':
    main()
