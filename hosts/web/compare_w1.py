"""Acceptance of W1: the geometry of the web host against xemu.

    python hosts/web/compare_w1.py [--runs w1_t2000 ...] [--out docs/vectors/w1_frames.json]

Every run in out/xemu/runs/w1_t* comes from tools/xemu/frame_at.py: one
frame of the BIOS in xemu with all the ticks that led to it, the drawing
surface 1280 x 480 and its depth. For each the web host is given the same
ticks (hosts/web/shot.mjs --ticks @capture.json --full) and its drawing
surface and depth are read back. Compared:

  t        the page's t after the ticks, as bit pattern, with the guest's
  depth    where the depth surface was written (depth < far): the outline
           of everything that writes depth, without any shading. Reported
           are both pixel counts, intersection over union, the pixels that
           differ and how many of them lie further than one pixel from the
           other outline, and on the pixels both cover the difference in
           depth in steps of the 24-bit surface.
  picture  where no draw writes depth (the logo as a picture, from t = 6):
           the green bodies, X and lettering: on both surfaces the pixels
           with green >= 8 and at least 8 above blue. This one measure
           looks at colour, because nothing else is left of the geometry.
           (In W1 the web host drew flat colours and gave only the meshes
           A, D and E such a colour; since W2 it draws the original's.)

Depth: xemu's surface holds depth in bits 8 to 31 of each word; the web
host's is float32 with 1 for far, scaled by 16777215 (SET_CLIP_MAX,
docs/web.md 8). Writes a sheet per frame to out/web/w1/<run>/sheet.png:
the web host's surface, xemu's, and the map of differences.
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
OUT = os.path.join(ROOT, 'out', 'web', 'w1')
W, H, FAR = 1280, 480, 16777215
LIT = 8             # green from which a pixel of a picture counts as a body
GREEN = 8           # and by how much its green must exceed its blue
DEEP = 256          # steps of depth from which a pixel counts as differing in depth
SOME = 2000         # fewer pixels than this in both masks: nothing to compare


def grow(m):
    """m widened by one pixel in every direction."""
    out = m.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def overlap(a, b):
    union = int((a | b).sum())
    differ = a ^ b
    # a differing pixel next to the other outline is an edge that fell the
    # other way; one further off is a body missing or too many
    off = int((a & ~grow(b)).sum() + (b & ~grow(a)).sum())
    return {'xemu': int(a.sum()), 'web': int(b.sum()),
            'iou': round(float((a & b).sum() / union), 5) if union else 1.0,
            'differ': int(differ.sum()), 'off_outline': off}


def compare(run):
    cap = json.load(open(os.path.join(RUNS, run, 'capture.json')))
    out = os.path.join(OUT, run)
    os.makedirs(out, exist_ok=True)
    subprocess.check_call(['node', 'hosts/web/shot.mjs', '--ticks', '@' + os.path.join(RUNS, run, 'capture.json'),
                           '--full', '--out', out], cwd=ROOT, stdout=subprocess.DEVNULL)
    meta = json.load(open(os.path.join(out, 'shots.json')))[0]
    stem = os.path.join(out, meta['png'][:-4])

    xz = (np.fromfile(os.path.join(RUNS, run, 'depth.bin'), '<u4').reshape(H, W) >> 8).astype(np.float64)
    wz = np.fromfile(stem + '_depth.bin', '<f4').reshape(H, W).astype(np.float64) * FAR
    xc = np.asarray(Image.open(os.path.join(RUNS, run, 'screen.png')).convert('RGB'))
    wc = np.asarray(Image.open(stem + '_screen.png').convert('RGB'))

    r = {'run': run, 't': round(cap['t'], 6), 't_bits': cap['t_bits'][-1], 'web_t_bits': meta['tBits'],
         'frames': cap['frames'], 'last_tick': cap['ticks'][-1], 'draws': meta['drawCount'], 'drawn': meta['drawn'],
         # the run itself: every step in ms, the ticks are their running sum
         'steps_ms': ' '.join(str(b - a) for a, b in zip([0] + cap['ticks'], cap['ticks']))}
    xm, wm = xz < FAR, wz < FAR
    r['depth'] = overlap(xm, wm)
    both = xm & wm
    if both.any():
        d = np.abs(xz - wz)[both]
        r['depth'].update({'steps_median': float(np.median(d)), 'steps_p99': float(np.percentile(d, 99)),
                           'deep': int((d > DEEP).sum())})
    # The logo's meshes B and C are large faces that their textures leave
    # mostly black (Logo.md 5.1), so "anything drawn" says nothing there.
    # Compared are the bodies that show as green in xemu, the X (meshes A and
    # D) and the lettering (E), by the same rule on both sides: green above
    # blue.
    xi, wi = xc.astype(np.int16), wc.astype(np.int16)
    xl = (xi[:, :, 1] >= LIT) & (xi[:, :, 1] - xi[:, :, 2] >= GREEN)
    wl = (wi[:, :, 1] >= LIT) & (wi[:, :, 1] - wi[:, :, 2] >= GREEN)
    r['picture'] = overlap(xl, wl)
    r['compared'] = ('depth' if max(r['depth']['xemu'], r['depth']['web']) >= SOME else
                     'picture' if max(r['picture']['xemu'], r['picture']['web']) >= SOME else 'nothing')

    a, b = (xm, wm) if r['compared'] == 'depth' else (xl, wl)
    diff = np.zeros((H, W, 3), np.uint8)
    diff[a & b] = (70, 70, 70)
    diff[a & ~b] = (255, 0, 0)          # xemu only
    diff[b & ~a] = (0, 90, 255)         # web only
    if r['compared'] == 'depth':
        diff[both & (np.abs(xz - wz) > DEEP)] = (255, 220, 0)
    sheet = Image.new('RGB', (W, 3 * H))
    for i, img in enumerate((wc, xc, diff)):
        sheet.paste(Image.fromarray(img), (0, i * H))
    sheet.save(os.path.join(out, 'sheet.png'))
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--runs', nargs='*')
    ap.add_argument('--out')
    a = ap.parse_args()
    runs = a.runs or sorted(os.path.basename(os.path.dirname(p))
                            for p in glob.glob(os.path.join(RUNS, 'w1_t*', 'capture.json')))
    rows, failed = [], 0
    for run in runs:
        r = compare(run)
        rows.append(r)
        m = r[r['compared']] if r['compared'] != 'nothing' else r['depth']
        same_t = r['t_bits'] == r['web_t_bits']
        failed += not same_t
        print('%s  t %.4f %s  frame %3d  %-7s  xemu %6d  web %6d  iou %.5f  differ %5d  off outline %5d%s'
              % (run, r['t'], 'same t' if same_t else 'T DIFFERS %s %s' % (r['t_bits'], r['web_t_bits']),
                 r['frames'], r['compared'], m['xemu'], m['web'], m['iou'], m['differ'], m['off_outline'],
                 '  depth steps median %.0f p99 %.0f, %d deep' % (m['steps_median'], m['steps_p99'], m['deep'])
                 if 'steps_median' in m and r['compared'] == 'depth' else ''))
    report = {'script': 'hosts/web/compare_w1.py', 'source': 'tools/xemu/frame_at.py, one run per frame',
              'lit_from': LIT, 'deep_above': DEEP, 'frames': rows}
    if a.out:
        with open(os.path.join(ROOT, a.out), 'w', newline='\n') as f:
            json.dump(report, f, indent=1)
            f.write('\n')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
