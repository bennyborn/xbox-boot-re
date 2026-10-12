"""Extracts tables of the small helpers (Overview.md, "Kleine Helfer") from the unpacked kernel.

    python tools/extract/overview.py

Reads out/rom/kernel.img (tools/rom/unpack.py) and writes

  data/overview/asin_factors.bin   0x80066030-0x800660F3 as it stands in the
                                   image: 0x31 float32, the factors of the
                                   series of asin_approx 0x80057E2D
  data/overview/asin_factors.json  the same, decoded

0x80057E2D reads the table backwards from entry 0x30 (0x800660F0) down to
entry 1; entry 0 is never read (Chamber.md 11). The table holds the ratios
of successive terms of the arcsine series: entry 0x30 is 1/6, entry 0x2F
is 0.45 = 9/20, and so on.
"""

import hashlib
import json
import os
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BASE = 0x80010000

ASIN_FACTORS = (0x80066030, 0x80066030 + 4 * 0x31)


def write(out, files):
    os.makedirs(out, exist_ok=True)
    for name, data in files.items():
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)
    with open(os.path.join(out, 'SHA256SUMS'), 'w', newline='\n') as f:
        for name in sorted(files):
            f.write('%s  %s\n' % (hashlib.sha256(files[name]).hexdigest(), name))


def main():
    img = open(os.path.join(ROOT, 'out', 'rom', 'kernel.img'), 'rb').read()
    lo, hi = ASIN_FACTORS
    raw = img[lo - BASE:hi - BASE]
    words = struct.unpack('<%dI' % (len(raw) // 4), raw)
    dec = {'source': 'out/rom/kernel.img, %#010x-%#010x' % ASIN_FACTORS,
           'read_by': '0x80057E2D, entries 0x30 down to 1',
           'factors': [{'entry': k, 'at': '%#010x' % (lo + 4 * k), 'bits': '%08x' % w,
                        'value': struct.unpack('<f', struct.pack('<I', w))[0]}
                       for k, w in enumerate(words)]}
    write(os.path.join(ROOT, 'data', 'overview'), {
        'asin_factors.bin': raw,
        'asin_factors.json': (json.dumps(dec, indent=1) + '\n').encode()})
    print('data/overview written')


if __name__ == '__main__':
    main()
