"""Collects the knockout runs of C3 into docs/vectors/c3_knockout.json.

    python tools/xemu/bench.py --name c3_ko_base --frames 60:420:90
    python tools/xemu/bench.py --name c3_ko_57c  --frames 60:420:90 --patch 0x8005bc63:c20800
    python tools/xemu/bench.py --name c3_ko_968  --frames 60:420:90 --patch 0x8005f897:c20800
    python tools/xemu/bench.py --name c3_ko_b5c  --frames 60:420:90 --patch 0x800583bf:c3
    python tools/xemu/bench.py --name c3_ko_17ec --frames 60:420:90 --patch 0x80059bcd:c20400
    python tools/xemu/vectors_c3_knockout.py

Each patch makes one draw function of the animation object return at once
(ret with the function's own stack cleanup). Per run: the patch as written
and read back, and per captured flip the SHA-256 of the scanned-out image.
What is missing on screen is an observation on the images
(tools/xemu/contact_sheet.py), written down in docs/decomp/Overview.md.
"""

import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RUNS = ['c3_ko_base', 'c3_ko_57c', 'c3_ko_968', 'c3_ko_b5c', 'c3_ko_17ec']


def main():
    out = []
    for name in RUNS:
        r = json.load(open(os.path.join(ROOT, 'out', 'xemu', 'runs', name, 'run.json')))
        out.append({'run': name, 'command': r['command'], 'inputs': r['inputs'],
                    'patches': r.get('patches', []),
                    'images': [{'flip': s.get('frame'), 'fb_sha256': s.get('fb_sha256')}
                               for s in r['snapshots']]})
    path = os.path.join(ROOT, 'docs', 'vectors', 'c3_knockout.json')
    with open(path, 'w') as f:
        json.dump(out, f, indent=1)
    print(path)


if __name__ == '__main__':
    main()
