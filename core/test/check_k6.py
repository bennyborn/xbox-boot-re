"""K6 checks that need Python: constants, the guest's command stream.

    python core/test/check_k6.py out/core/native/test_k6.exe

1. Every constant `#define K_* <value>f /* <address> */` in
   core/src/Effects.c and core/src/FrameOrder.c is compared with the float32
   at that address in out/rom/kernel.img (file offset = address -
   0x80010000, Overview.md); every `B_*` with the immediate at its address;
   every `VSH_*` and `PSB_*` must be the address of a program or a combiner
   block in data/gpu/gpu.json.
2. The frames `test_k6 --dump` writes, against the pushbuffer words
   probe_c8.py read in the run c8_main2 (out/xemu/runs/c8_main2/
   probe_c8.json, local only). The words of a frame are decoded with
   tools/xemu/nv2a.py and replayed into the state a draw sees:
     - the vertex constants (SET_TRANSFORM_CONSTANT_LOAD and
       SET_TRANSFORM_CONSTANT, as xemu's pgraph.c applies them),
     - the combiner factors (SET_COMBINER_FACTOR0/1, SET_SPECULAR_FOG_FACTOR),
     - SET_MODEL_VIEW_MATRIX.
   At every SET_BEGIN_END block the core's draw of the same position must
   name, bit for bit, the constants in effect; its colours, packed as
   0x800410B0 packs them and routed by the words 57 to 59 of the block
   (GPU.md 4.2), must be the factors in effect; its factor colour must be
   SET_COMBINER_FACTOR0[0]. For fixed-function draws the model-view matrix
   in the stream must be (world * view) transposed, with the product in
   float32, the four terms of an element summed in the order 0, 1, 2, 3
   (GPU.md 2.1).
   Left out: w of the constants 9 and 11 of the blob's mesh, which
   blob_draw does not set (Blob.md 7.2).
"""

import json
import os
import re
import struct
import subprocess
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'xemu'))
import nv2a  # noqa: E402
import vectors_c8 as v8  # noqa: E402
import effects_model as em  # noqa: E402

KERNEL = os.path.join(ROOT, 'out', 'rom', 'kernel.img')
RUN = os.path.join(ROOT, 'out', 'xemu', 'runs', 'c8_main2', 'probe_c8.json')
PACK = os.path.join(ROOT, 'out', 'pack', 'xboot.pak')
VECTOR = os.path.join(ROOT, 'docs', 'vectors', 'c8_gpu.json')
GPU = os.path.join(ROOT, 'data', 'gpu', 'gpu.json')
TMP = os.path.join(ROOT, 'out', 'core', 'k6')
BASE = 0x80010000

DRAW_BLOB_MESH = 3          # XB_DRAW_BLOB_MESH
FLAG_FACTOR, FLAG_MATRICES = 2, 4

checks = failed = 0


def check(ok, what):
    global checks, failed
    checks += 1
    failed += not ok
    print(('ok    ' if ok else 'FAIL  ') + what)


def fb(x):
    return struct.unpack('<I', struct.pack('<f', float(x)))[0]


def bf(u):
    return struct.unpack('<f', struct.pack('<I', u))[0]


def is_nan(u):
    return (u & 0x7F800000) == 0x7F800000 and (u & 0x007FFFFF) != 0


# ---------------------------------------------------------------- 1. constants

def constants():
    img = open(KERNEL, 'rb').read()
    gpu = json.load(open(GPU))
    objs = {int(o['addr'], 16) for o in gpu['programs']} | {int(o['addr'], 16) for o in gpu['combiners']}
    total_k = total_b = total_o = 0
    bad = []
    for name in ('Effects.c', 'FrameOrder.c'):
        src = open(os.path.join(ROOT, 'core', 'src', name), encoding='utf-8').read()
        for cname, value, addr in re.findall(
                r'#define\s+(K_\w+)\s+(-?[0-9.]+(?:e[-+]?[0-9]+)?)f\s+/\*\s*(0x[0-9A-Fa-f]{8})', src):
            total_k += 1
            rom = struct.unpack_from('<f', img, int(addr, 16) - BASE)[0]
            if fb(rom) != fb(np.float32(float(value))):
                bad.append(f'{name} {cname} {value} at {addr}: kernel {rom!r}')
        for cname, value, addr in re.findall(
                r'#define\s+(B_\w+)\s+(0x[0-9A-Fa-f]+)u\s+/\*\s*(0x[0-9A-Fa-f]{8}), imm32', src):
            total_b += 1
            got = struct.unpack_from('<I', img, int(addr, 16) - BASE)[0]
            if got != int(value, 16):
                bad.append(f'{name} {cname} {value} at {addr}: code {got:#x}')
        for cname, value in re.findall(r'#define\s+((?:VSH|PSB)_\w+)\s+(0x[0-9A-Fa-f]{8})u', src):
            total_o += 1
            if int(value, 16) not in objs:
                bad.append(f'{name} {cname} {value}: not an object of data/gpu/gpu.json')
    for b in bad:
        print('      ' + b)
    check(total_k == 39 and total_b == 2 and total_o == 14 and not bad,
          f'{total_k} constants and {total_b} immediates of Effects.c and FrameOrder.c equal the kernel image, '
          f'{total_o} GPU objects are in data/gpu/gpu.json, {len(bad)} differ')


# ---------------------------------------------------------------- 2. the stream

def guest_draws(p, fi):
    """The state at every SET_BEGIN_END block of frame fi: vertex constants,
    combiner factors, model-view matrix. Only subchannel 0 (the 3D object)."""
    bank, reg = {}, {}
    load = 0
    out = []
    for c in p['calls']:
        if c['frame'] != fi:
            continue
        for item in v8.methods_of(c):
            m, v = item[0], item[1]
            if len(item) > 2 and item[2] != 0:
                continue
            if m == 0x1EA4:                         # SET_TRANSFORM_CONSTANT_LOAD
                load = v
            elif 0xB80 <= m < 0xC00:                # SET_TRANSFORM_CONSTANT
                slot = (m - 0xB80) // 4
                bank.setdefault(load, [None] * 4)[slot % 4] = v
                if slot % 4 == 3:
                    load += 1
            elif 0xA60 <= m < 0xAA0 or m in (0x1E20, 0x1E24) or 0x480 <= m < 0x4C0:
                reg[m] = v
            elif m == nv2a.BEGIN_END and v:
                out.append(({k: list(w) for k, w in bank.items()}, dict(reg)))
    return out


def block_routes():
    """Combiner block address -> list of (constant, register)."""
    gpu = json.load(open(GPU))
    routes = {}
    for cb in gpu['combiners']:
        w = [int(x, 16) for x in cb['words']]
        r = []
        for i in range(8):
            for word, base in ((w[57], 0xA60), (w[58], 0xA80)):
                k = (word >> (4 * i)) & 0xF
                if k != 0xF:
                    r.append((k, base + 4 * i))
        for i, regno in ((0, 0x1E20), (1, 0x1E24)):
            k = (w[59] >> (4 * i)) & 0xF
            if k != 0xF:
                r.append((k, regno))
        routes[int(cb['addr'], 16)] = r
    return routes


STEPS = ['A1', 'A2', 'U1', 'U2', 'U3', 'U4', 'D1', 'D2', 'D3', 'D4', 'D5']


def m_mul32(a, b):
    """a * b in float32, the four products of an element summed in the
    order k = 0, 1, 2, 3 and rounded after every step: what the library's
    model-view matrix in the stream turns out to be (GPU.md 2.1)."""
    a = np.array(a, dtype=np.float32)
    b = np.array(b, dtype=np.float32)
    out = np.zeros(16, dtype=np.float32)
    with np.errstate(all='ignore'):
        for e in range(16):
            r, c = divmod(e, 4)
            acc = np.float32(a[r * 4] * b[c])
            for k in (1, 2, 3):
                acc = np.float32(acc + np.float32(a[r * 4 + k] * b[k * 4 + c]))
            out[e] = acc
    return out.reshape(4, 4)


def stream(exe):
    if not os.path.exists(RUN):
        print('      ' + os.path.relpath(RUN, ROOT) + ' is not here; the stream is not compared')
        return
    os.makedirs(TMP, exist_ok=True)
    subprocess.run([os.path.abspath(exe), PACK, VECTOR, '--dump', TMP], cwd=ROOT, check=False, capture_output=True)
    p = json.load(open(RUN))
    v8.drop_repeats(p)
    vec = json.load(open(VECTOR))
    routes = block_routes()
    tot = {s: {'draws': 0, 'vs': 0, 'vs_bad': 0, 'vs_nan': 0, 'vs_unsent': 0, 'ps': 0, 'ps_bad': 0,
               'ps_unsent': 0, 'factor': 0, 'factor_bad': 0, 'mv': 0, 'mv_exact': 0}
           for s in STEPS}
    count_bad = []
    first_bad = []
    for f in vec['draws']['frames']:
        fi = f['frame']
        core = json.load(open(os.path.join(TMP, f'frame_{fi}.json')))
        guest = guest_draws(p, fi)
        if len(core) != len(guest):
            count_bad.append((fi, len(core), len(guest)))
            continue
        for di, (x, (bank, reg)) in enumerate(zip(core, guest)):
            t = tot[x['step']]
            t['draws'] += 1
            for base, words in x['vs']:
                for i in range(len(words) // 4):
                    k = base + i
                    got = bank.get(96 + k)
                    for a in range(4):
                        if x['what'] == DRAW_BLOB_MESH and k in (9, 11) and a == 3:
                            continue
                        t['vs'] += 1
                        w = words[4 * i + a]
                        if got is None or got[a] is None:
                            t['vs_unsent'] += 1
                        elif got[a] != w:
                            if is_nan(got[a]) and is_nan(w):
                                t['vs_nan'] += 1
                            else:
                                t['vs_bad'] += 1
                                if len(first_bad) < 12:
                                    first_bad.append(f"frame {fi} draw {di} {x['step']} what {x['what']} part "
                                                     f"{x['part']}: constant {k}.{a} core {w:08X} "
                                                     f"({bf(w):.7g}), stream {got[a]:08X} ({bf(got[a]):.7g})")
            if x['ps']:
                for k, regno in routes.get(x['combiners'], []):
                    if 4 * k + 4 > len(x['ps']):
                        continue
                    t['ps'] += 1
                    want = em.pack_colour([bf(u) for u in x['ps'][4 * k:4 * k + 4]])
                    if regno not in reg:
                        t['ps_unsent'] += 1
                    elif reg[regno] != want:
                        t['ps_bad'] += 1
                        if len(first_bad) < 12:
                            first_bad.append(f"frame {fi} draw {di} {x['step']}: colour {k} packs to "
                                             f"{want:08X}, register {regno:#x} holds {reg[regno]:08X}")
            if x['flags'] & FLAG_FACTOR:
                t['factor'] += 1
                if reg.get(0xA60) != x['factor']:
                    t['factor_bad'] += 1
                    if len(first_bad) < 12:
                        first_bad.append(f"frame {fi} draw {di} {x['step']}: factor {x['factor']:08X}, "
                                         f"stream {reg.get(0xA60)}")
            if x['flags'] & FLAG_MATRICES and all(0x480 + 4 * i in reg for i in range(16)):
                mv = m_mul32([bf(u) for u in x['world']], [bf(u) for u in x['view']]).T.reshape(16)
                t['mv'] += 1
                if all(fb(a) == reg[0x480 + 4 * i] or (a != a and is_nan(reg[0x480 + 4 * i]))
                       for i, a in enumerate(mv)):
                    t['mv_exact'] += 1
    for b in first_bad:
        print('      ' + b)
    check(not count_bad, f'{len(vec["draws"]["frames"])} recorded frames: the core lists as many draws as the '
          f'stream has SET_BEGIN_END blocks ({count_bad or "all equal"})')
    for s in STEPS:
        t = tot[s]
        if not t['draws']:
            continue
        check(not t['vs_bad'] and not t['ps_bad'] and not t['factor_bad'] and not t['vs_unsent']
              and not t['ps_unsent'],
              f"{s}: {t['draws']} draws; {t['vs']} constant values, {t['vs_bad']} differ "
              f"({t['vs_nan']} NaN on both sides with other bits, {t['vs_unsent']} not sent in the frame); "
              f"{t['ps']} packed colours, {t['ps_bad']} differ; {t['factor']} factor colours, "
              f"{t['factor_bad']} differ")
        if t['mv']:
            check(t['mv_exact'] == t['mv'],
                  f"{s}: model-view matrix of {t['mv']} fixed-function draws: {t['mv_exact']} equal "
                  f"float32(world * view) transposed, bit for bit")


def main():
    exe = sys.argv[1]
    constants()
    stream(exe)
    print(f'{checks} checks, {failed} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
