"""Acceptance of W2: the colours of the web host against xemu.

    python hosts/web/compare_w2.py [--runs w1_t2000 ...] [--off shadow] [--out docs/vectors/w2_frames.json]

Every run in out/xemu/runs/w1_t* and w2_t* comes from tools/xemu/frame_at.py:
one frame of the BIOS in xemu with all the ticks that led to it and the
drawing surface 1280 x 480 as xemu drew it. The web host is given the same
ticks and its drawing surface is read back (hosts/web/shot.mjs --ticks
@capture.json --full). Compared are the two surfaces, 8 bits per channel,
with the measures of docs/web.md 7:

  mad   mean absolute difference per channel, 0 ... 255
  far   share of the pixels whose largest channel difference is above 16
  iou   overlap of the masks "largest channel >= 24"
  p50, p99, max   of the largest channel difference per pixel
  mean  mean colour of both surfaces

--off leaves parts of the web host's picture out (renderer.js), to see what
a part is worth in these numbers. Writes a sheet per frame to
out/web/w2/<run>/sheet.png: the web host's surface, xemu's, and their
difference times four.
"""

import argparse
import glob
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')
OUT = os.path.join(ROOT, 'out', 'web', 'w2')
W, H = 1280, 480
FAR = 16            # channel difference above which a pixel counts as far off
LIT = 24            # largest channel from which a pixel belongs to the mask


def compare(run, off, out_root):
    cap = json.load(open(os.path.join(RUNS, run, 'capture.json')))
    out = os.path.join(out_root, run)
    os.makedirs(out, exist_ok=True)
    cmd = ['node', 'hosts/web/shot.mjs', '--ticks', '@' + os.path.join(RUNS, run, 'capture.json'), '--full', '--out', out]
    if off:
        cmd += ['--off', off]
    subprocess.check_call(cmd, cwd=ROOT, stdout=subprocess.DEVNULL)
    meta = json.load(open(os.path.join(out, 'shots.json')))[0]
    xc = np.asarray(Image.open(os.path.join(RUNS, run, 'screen.png')).convert('RGB')).astype(np.int16)
    wc = np.asarray(Image.open(os.path.join(out, meta['png'][:-4] + '_screen.png')).convert('RGB')).astype(np.int16)
    diff = np.abs(xc - wc)
    worst = diff.max(axis=2)
    xm, wm = xc.max(axis=2) >= LIT, wc.max(axis=2) >= LIT
    union = int((xm | wm).sum())
    r = {'run': run, 't': round(cap['t'], 6), 't_bits': cap['t_bits'][-1], 'web_t_bits': meta['tBits'],
         'frames': cap['frames'], 'draws': meta['drawCount'], 'drawn': meta['drawn'],
         'mad': round(float(diff.mean()), 3), 'far': round(float((worst > FAR).mean()), 5),
         'iou': round(float((xm & wm).sum() / union), 5) if union else 1.0,
         'mask_xemu': int(xm.sum()), 'mask_web': int(wm.sum()),
         'p50': int(np.percentile(worst, 50)), 'p99': int(np.percentile(worst, 99)), 'max': int(worst.max()),
         'mean_xemu': [round(float(v), 2) for v in xc.reshape(-1, 3).mean(axis=0)],
         'mean_web': [round(float(v), 2) for v in wc.reshape(-1, 3).mean(axis=0)]}
    sheet = Image.new('RGB', (W, 3 * H))
    for i, img in enumerate((wc, xc, np.minimum(diff * 4, 255))):
        sheet.paste(Image.fromarray(img.astype(np.uint8)), (0, i * H))
    sheet.save(os.path.join(out, 'sheet.png'))
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--runs', nargs='*')
    ap.add_argument('--off', default='')
    ap.add_argument('--out')
    a = ap.parse_args()
    runs = a.runs or sorted(os.path.basename(os.path.dirname(p)) for pat in ('w1_t*', 'w2_t*')
                            for p in glob.glob(os.path.join(RUNS, pat, 'capture.json')))
    runs.sort(key=lambda r: r.split('_t')[1])
    rows, failed = [], 0
    for run in runs:
        r = compare(run, a.off, OUT + ('_off_' + a.off.replace(',', '_') if a.off else ''))
        rows.append(r)
        same_t = r['t_bits'] == r['web_t_bits']
        failed += not same_t
        print('%s  t %.4f %s  mad %6.3f  far %.4f  iou %.4f  p50 %3d p99 %3d max %3d  mean xemu %s web %s'
              % (run, r['t'], 'same t' if same_t else 'T DIFFERS', r['mad'], r['far'], r['iou'], r['p50'], r['p99'],
                 r['max'], r['mean_xemu'], r['mean_web']), flush=True)
    report = {'script': 'hosts/web/compare_w2.py', 'source': 'tools/xemu/frame_at.py, one run per frame',
              'off': a.off, 'far_above': FAR, 'mask_from': LIT, 'frames': rows}
    if a.out:
        with open(os.path.join(ROOT, a.out), 'w', newline='\n') as f:
            json.dump(report, f, indent=1)
            f.write('\n')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
