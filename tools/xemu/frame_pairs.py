"""How far two xemu runs of the same BIOS differ in their pictures.

    python tools/xemu/frame_pairs.py w0_A w0_B w0_C --out docs/vectors/w0_xemu_pairs.json

The runs come from bench.py with --frames and --peek 0x8006F050:0x30, so
every captured picture carries the animation object's first 0x30 bytes and
with them t (this+0x10, docs/decomp/Timing.md 2). Two runs never capture
the same t. For every two runs each picture of the
first is therefore paired with the picture of the second whose t is
nearest, and the difference is reported together with the distance in t,
summed up in classes of that distance. This is the floor under which no
comparison of a port with xemu can fall.

The t read from the snapshot is that of the animation object at the moment
of the save. The CPU may by then have advanced it for the next frame, so it
can lie one frame ahead of the picture.

Measures between two pictures of 640 x 480, 8 bit per channel:

    mad    mean absolute difference per channel, 0 ... 255
    far    share of pixels whose largest channel difference exceeds 16
    iou    overlap of the two masks "largest channel >= 24": intersection
           over union, 1 for equal masks and for two empty ones
"""

import argparse
import json
import os
import struct

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')
ANIM = '0x8006f050'
FAR, MASK = 16, 24


def measures(a, b):
    """a, b: uint8 arrays height x width x 3."""
    d = np.abs(a.astype(np.int16) - b.astype(np.int16))
    ma, mb = a.max(axis=2) >= MASK, b.max(axis=2) >= MASK
    union = int((ma | mb).sum())
    return {'mad': round(float(d.mean()), 4),
            'far': round(float((d.max(axis=2) > FAR).mean()), 5),
            'iou': round(float((ma & mb).sum() / union), 5) if union else 1.0,
            'mask': [int(ma.sum()), int(mb.sum())]}


def pictures(run):
    """Captured pictures of a run while the animation runs, sorted by t."""
    log = json.load(open(os.path.join(RUNS, run, 'run.json')))
    out = []
    for s in log['snapshots']:
        if 'png' not in s or ANIM not in s.get('peek', {}):
            continue
        obj = bytes.fromhex(s['peek'][ANIM])
        t, = struct.unpack_from('<f', obj, 0x10)
        tick, = struct.unpack_from('<I', obj, 0x28)
        out.append({'flip': s['frame'], 't': t, 'tick': tick, 'savevm_ms': s['savevm_ms'],
                    'png': os.path.join(RUNS, run, s['png'])})
    return sorted(out, key=lambda p: (p['t'], p['flip'] or 0))


def load(p):
    return np.asarray(Image.open(p['png']).convert('RGB'))


# Pairs are summed up by how far their t lie apart, in ms.
CLASSES = ((0, 2), (2, 10), (10, 30), (30, 1000))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('runs', nargs='+', help='two or more runs; every two of them are paired')
    ap.add_argument('--out')
    a = ap.parse_args()
    pics = {r: pictures(r) for r in a.runs}
    rows = []
    for i, ra in enumerate(a.runs):
        for rb in a.runs[i + 1:]:
            for p in pics[ra]:
                if not 0 < p['t'] < 8:
                    continue
                q = min(pics[rb], key=lambda x: abs(x['t'] - p['t']))
                m = measures(load(p), load(q))
                rows.append({'a': ra, 'b': rb, 't_a': round(p['t'], 6), 't_b': round(q['t'], 6),
                             'dt_ms': round((q['t'] - p['t']) * 1000, 3),
                             'flip_a': p['flip'], 'flip_b': q['flip'], **m})
                print('%s %s  t %8.4f / %8.4f  dt %8.2f ms  flips %4s %4s  mad %7.3f  far %.4f  iou %.4f'
                      % (ra, rb, p['t'], q['t'], rows[-1]['dt_ms'], p['flip'], q['flip'], m['mad'], m['far'], m['iou']))
    report = {'runs': a.runs, 'source': 'tools/xemu/bench.py --frames A:520:10 --peek 0x8006F050:0x30',
              'script': 'tools/xemu/frame_pairs.py', 'far_above': FAR, 'mask_from': MASK,
              'captured': {r: [[p['flip'], round(p['t'], 6), p['tick']] for p in pics[r]] for r in a.runs},
              'classes': [], 'pairs': rows}
    for lo, hi in CLASSES:
        sel = [r for r in rows if lo <= abs(r['dt_ms']) < hi]
        c = {'dt_ms': [lo, hi], 'pairs': len(sel)}
        if sel:
            c.update({'mad_max': max(r['mad'] for r in sel), 'far_max': max(r['far'] for r in sel),
                      'iou_min': min(r['iou'] for r in sel),
                      'mad_median': float(np.median([r['mad'] for r in sel]))})
            print('|dt| %4d ... %4d ms: %3d pairs, mad up to %7.3f (median %.3f), far up to %.4f, iou from %.4f'
                  % (lo, hi, len(sel), c['mad_max'], c['mad_median'], c['far_max'], c['iou_min']))
        report['classes'].append(c)
    if a.out:
        with open(os.path.join(ROOT, a.out), 'w') as f:
            json.dump(report, f, indent=1)
            f.write('\n')


if __name__ == '__main__':
    main()
