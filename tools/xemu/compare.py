"""Compare two bench runs at every frame both captured.

    python tools/xemu/compare.py RUN_A RUN_B [--out DIR]

RUN_A and RUN_B are names under out/xemu/runs/. For each frame_NNNN that
exists in both, the guest RAM is compared byte by byte and the differing
bytes are merged into ranges (gaps under 16 bytes are closed). Each range
is labelled with the region it falls into:

  kernel <section>  the kernel image at physical 0x10000, where the
                    snapshot holds it (Overview.md: base 0x80010000; the
                    kernel's .text is found at physical 0x10000 in RAM)
  surface <addr>    a colour or depth surface xemu created
                    (surface_create_color / _zeta in the run's trace, or
                    the zeta offset in the saved PGRAPH state), extent
                    pitch * height
  pushbuffer        between the lowest and highest PFIFO DMA_GET/DMA_PUT
                    seen in either run's snapshots, widened to 64 KiB; an
                    approximation until the ring bounds are known
  other             anything else

The device states (state.json) are compared field by field; lists are
compared element by element and reported by index.
"""

import argparse
import json
import os
import re
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')

# Kernel sections from docs/decomp/Overview.md (virtual), minus 0x80000000.
KERNEL = [('.text', 0x800102C0, 0x800358F0), ('.data', 0x80035900, 0x8003AECC),
          ('STICKY', 0x8003AEE0, 0x8003C870), ('IDEXPRDT', 0x8003CA00, 0x8003CB08),
          ('INIT', 0x8003CB20, 0x800AB4FA)]
KERNEL = [(n, a - 0x80000000, b - 0x80000000) for n, a, b in KERNEL]
KERNEL_HEADERS = (0x10000, 0x102C0)
CREATE_RE = re.compile(
    rb'nv2a_pgraph_surface_create_(?:color|zeta) +Create: \[(?:COLOR|ZETA) +@ 0x([0-9a-f]+) '
    rb'\((\d+)x(\d+)\)\].*?p=(\d+)')
NV_PFIFO_CACHE1_DMA_PUT, NV_PFIFO_CACHE1_DMA_GET = 0x1240, 0x1244


def surfaces(run):
    out = {}
    with open(os.path.join(RUNS, run, 'trace.log'), 'rb') as f:
        for m in CREATE_RE.finditer(f.read()):
            a, h, p = int(m[1], 16), int(m[3]), int(m[4])
            out[a] = max(out.get(a, 0), p * h)
    return out


def nv2a(state):
    return next(v for k, v in state.items() if '/nv2a (' in k)


def regions(runs_states):
    """(start, end, name) for every known region, kernel first."""
    out = [(s, e, f'kernel {n}') for n, s, e in KERNEL]
    out.append((*KERNEL_HEADERS, 'kernel headers'))
    surf, pb = {}, []
    for run, states in runs_states:
        for a, size in surfaces(run).items():
            surf[a] = max(surf.get(a, 0), size)
        for st in states:
            g = nv2a(st)
            z = int(g['pgraph.surface_zeta.offset'], 16)
            if z:
                size = (int(g['pgraph.surface_zeta.pitch'], 16)
                        * int(g['pgraph.surface_shape.clip_height'], 16))
                surf[z] = max(surf.get(z, 0), size)
            r = g['pfifo.regs']
            pb += [int(r[NV_PFIFO_CACHE1_DMA_GET], 16), int(r[NV_PFIFO_CACHE1_DMA_PUT], 16)]
    out += [(a, a + size, f'surface {a:#x}') for a, size in sorted(surf.items())]
    if pb:
        out.append((min(pb) & ~0xFFFF, (max(pb) + 0xFFFF) & ~0xFFFF, 'pushbuffer'))
    return out


def label(a, b, regs):
    for s, e, name in regs:
        if a < e and b > s:
            return name
    return 'other'


def ranges(x, y, gap=16):
    d = np.flatnonzero(x != y)
    if not d.size:
        return []
    cut = np.flatnonzero(np.diff(d) > gap)
    starts = np.r_[d[0], d[cut + 1]]
    ends = np.r_[d[cut], d[-1]] + 1
    return [(int(s), int(e), int((x[s:e] != y[s:e]).sum())) for s, e in zip(starts, ends)]


def diff_state(a, b, path=''):
    out = []
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                out.append((f'{path}/{k}', a.get(k), b.get(k)))
            else:
                out += diff_state(a[k], b[k], f'{path}/{k}')
    elif isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        for i, (u, v) in enumerate(zip(a, b)):
            out += diff_state(u, v, f'{path}[{i}]')
    elif a != b:
        out.append((path, a, b))
    return out


def frames(run):
    d = os.path.join(RUNS, run)
    return {int(n[6:]) for n in os.listdir(d) if re.fullmatch(r'frame_\d{4}', n)}


def compare(ra, rb, n, regs, sa, sb):
    fa = os.path.join(RUNS, ra, f'frame_{n:04d}')
    fb = os.path.join(RUNS, rb, f'frame_{n:04d}')
    x = np.fromfile(os.path.join(fa, 'ram.bin'), np.uint8)
    y = np.fromfile(os.path.join(fb, 'ram.bin'), np.uint8)
    rs = [{'start': f'{s:#010x}', 'end': f'{e:#010x}', 'bytes_differing': c,
           'region': label(s, e, regs)} for s, e, c in ranges(x, y)]
    by_region = {}
    for r in rs:
        g = by_region.setdefault(r['region'], {'ranges': 0, 'bytes_differing': 0})
        g['ranges'] += 1
        g['bytes_differing'] += r['bytes_differing']
    devices = {}
    for dev in sorted(set(sa) | set(sb)):
        d = diff_state(sa.get(dev), sb.get(dev))
        if d:
            devices[dev] = [{'field': p, 'a': u, 'b': v} for p, u, v in d]
    return {'frame': n, 'ram_equal': not rs,
            'ram_bytes_differing': int(sum(r['bytes_differing'] for r in rs)),
            'ram_by_region': by_region, 'ram_ranges': rs,
            'state_fields_differing': sum(len(v) for v in devices.values()),
            'state_by_device': devices}


def load_state(run, n):
    return json.load(open(os.path.join(RUNS, run, f'frame_{n:04d}', 'state.json')))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--out')
    a = ap.parse_args()
    common = sorted(frames(a.a) & frames(a.b))
    if not common:
        sys.exit(f'no frame captured in both runs: {sorted(frames(a.a))} vs {sorted(frames(a.b))}')
    out = a.out or os.path.join(ROOT, 'out', 'xemu', 'compare', f'{a.a}__{a.b}')
    os.makedirs(out, exist_ok=True)
    states = {n: (load_state(a.a, n), load_state(a.b, n)) for n in common}
    regs = regions([(a.a, [v[0] for v in states.values()]),
                    (a.b, [v[1] for v in states.values()])])
    with open(os.path.join(out, 'regions.json'), 'w') as f:
        json.dump([{'start': f'{s:#010x}', 'end': f'{e:#010x}', 'name': n}
                   for s, e, n in regs], f, indent=1)
    for n in common:
        r = compare(a.a, a.b, n, regs, *states[n])
        with open(os.path.join(out, f'frame_{n:04d}.json'), 'w') as f:
            json.dump(r, f, indent=1)
        print(f"frame {n}: RAM {'equal' if r['ram_equal'] else 'differs'}, "
              f"{r['ram_bytes_differing']} bytes in {len(r['ram_ranges'])} ranges; "
              f"{r['state_fields_differing']} device state fields differ")
        for k, v in sorted(r['ram_by_region'].items()):
            print(f'  {k:24} {v["ranges"]:5} ranges {v["bytes_differing"]:9} bytes')
        for dev, fs in sorted(r['state_by_device'].items()):
            print(f'  state {dev}: {len(fs)} fields, e.g. {fs[0]["field"]}')
    print('report', out)


if __name__ == '__main__':
    main()
