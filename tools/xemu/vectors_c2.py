"""Write docs/vectors/c2_bench.json from the C2 runs.

    python tools/xemu/bench.py --name c2_break2bl --break 0x9024C
    python tools/xemu/bench.py --name c2final_A --frame 90 --frame 150 --frame 210 --frame 300 --video --audio --duration 16
    python tools/xemu/bench.py --name c2final_B (same arguments)
    python tools/xemu/compare.py c2final_A c2final_B
    python tools/xemu/vectors_c2.py
"""

import hashlib
import json
import os
from collections import Counter

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')
CMP = os.path.join(ROOT, 'out', 'xemu', 'compare', 'c2final_A__c2final_B')
OUT = os.path.join(ROOT, 'docs', 'vectors', 'c2_bench.json')
VBLANK_MS = 1000 / 60


def run(name):
    return json.load(open(os.path.join(RUNS, name, 'run.json')))


def break_2bl():
    r = run('c2_break2bl')
    ram = open(os.path.join(RUNS, 'c2_break2bl', 'break_0009024c', 'ram.bin'), 'rb').read()
    m = ram[0x90000:0x96000]
    own = open(os.path.join(ROOT, 'out', 'rom', '2bl.bin'), 'rb').read()
    return {'run': 'c2_break2bl', 'eip_gdb': r['break_regs_gdb']['eip'],
            'ram_0x90000_0x96000_sha256': hashlib.sha256(m).hexdigest(),
            'equals_out_rom_2bl_bin': m == own}


def kernel_text_in_ram():
    ram = open(os.path.join(RUNS, 'c2final_A', 'frame_0090', 'ram.bin'), 'rb').read()
    img = open(os.path.join(ROOT, 'out', 'rom', 'kernel.img'), 'rb').read()
    a, b = 0x800102C0 - 0x80010000, 0x800358F0 - 0x80010000
    x = np.frombuffer(ram[0x10000 + a:0x10000 + b], np.uint8)
    y = np.frombuffer(img[a:b], np.uint8)
    d = np.flatnonzero(x != y)
    return {'run': 'c2final_A', 'frame': 90, 'section': '.text',
            'physical': [f'{0x10000 + a:#x}', f'{0x10000 + b:#x}'],
            'bytes': int(x.size), 'bytes_differing_from_kernel_img': int(d.size),
            'first_differing_va': [f'{0x80010000 + a + int(i):#x}' for i in d[:8]]}


def flip_timing(name):
    f = json.load(open(os.path.join(RUNS, name, 'flips.json')))
    snaps = {s['frame'] for s in run(name)['snapshots']}
    # Intervals in vblanks between consecutive flips, leaving out the
    # flips right after a snapshot (the save pauses the VM).
    iv = [b['vblanks'] - a['vblanks'] for a, b in zip(f, f[1:])
          if a['n'] not in snaps and b['n'] not in snaps]
    return {'run': name, 'flips': len(f), 'vblanks_at_last_flip': f[-1]['vblanks'],
            'interval_vblanks_histogram': dict(sorted(Counter(iv).items())),
            'flips_with_interval_2': [b['n'] for a, b in zip(f, f[1:])
                                      if b['vblanks'] - a['vblanks'] == 2
                                      and a['n'] not in snaps and b['n'] not in snaps][:40]}


def determinism():
    ra, rb = run('c2final_A'), run('c2final_B')
    sa = {s['frame']: s for s in ra['snapshots']}
    sb = {s['frame']: s for s in rb['snapshots']}
    out = []
    for n in sorted(set(sa) & set(sb)):
        c = json.load(open(os.path.join(CMP, f'frame_{n:04d}.json')))
        ia = np.asarray(Image.open(os.path.join(RUNS, 'c2final_A', f'frame_{n:04d}', 'frame.png'))).astype(int)
        ib = np.asarray(Image.open(os.path.join(RUNS, 'c2final_B', f'frame_{n:04d}', 'frame.png'))).astype(int)
        out.append({
            'frame': n,
            'vblanks': {'A': sa[n]['vblanks'], 'B': sb[n]['vblanks']},
            'methods': {'A': sa[n].get('methods'), 'B': sb[n].get('methods')},
            'ram_sha256': {'A': sa[n]['ram_sha256'], 'B': sb[n]['ram_sha256']},
            'ram_bytes_differing': c['ram_bytes_differing'],
            'ram_by_region': {k: v['bytes_differing'] for k, v in sorted(c['ram_by_region'].items())},
            'image_mean_abs_diff': round(float(np.abs(ia - ib).mean()), 3),
            'image_pixels_differing': int((np.abs(ia - ib).max(axis=2) > 0).sum()),
            'state_fields_differing': {k.split(' (')[0]: len(v)
                                       for k, v in sorted(c['state_by_device'].items())},
        })
    return out


def main():
    v = {'source': 'tools/xemu/vectors_c2.py; runs in out/xemu/runs/, see docstring',
         'xemu_sha256': run('c2final_A')['inputs']['xemu']['sha256'],
         'break_2bl': break_2bl(),
         'kernel_text_in_ram': kernel_text_in_ram(),
         'flip_timing': [flip_timing('c2final_A'), flip_timing('c2final_B')],
         'determinism': determinism()}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w') as f:
        json.dump(v, f, indent=1)
    print(json.dumps(v, indent=1)[:4000])


if __name__ == '__main__':
    main()
