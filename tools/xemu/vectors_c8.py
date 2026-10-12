"""Checks the C8 runs and writes docs/vectors/c8_gpu.json.

    python tools/xemu/vectors_c8.py [c8_main2]

For a run of probe_c8.py:

1. Stream: per recorded frame (and the setup) the pushbuffer words of all
   calls, decoded with tools/xemu/nv2a.py, against the methods the GPU ran
   (trace nv2a_pgraph_method*). The start in the trace is the place from
   which the longest stretch matches; jump words are skipped (the GPU
   follows them), runs of ARRAY_ELEMENT16 are compared by their word count,
   and END, BEGIN that xemu folds between two DRAW_ARRAYS are allowed to
   be missing (tools/xemu/README.md, "Limits").
2. Textures: every texture the setup makes on the CPU against
   tools/xemu/texture_model.py; the SHA-256 of each dump goes into the
   vector, so that the core's host tests can compare without the run.
3. GPU objects: every call of 0x80040440 that loads a vertex program and
   every call of 0x80040EF0 with a combiner block, against data/gpu/gpu.json.
4. Draws: every SET_BEGIN_END block of every recorded frame with the call
   that sent it and the step of docs/decomp/FrameOrder.md (STEPS).
5. Effects: the constants, colours and glow values fx_update and fx_draw
   send in every recorded frame, against tools/xemu/effects_model.py.
6. Timeline: t before every call of 0x80057949 as the probe read it, as
   the step in whole milliseconds and the float32 after the step, so that
   the core's host tests can replay the run frame by frame without it.
"""

import hashlib
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nv2a  # noqa: E402
import texture_model as tm  # noqa: E402
from chamber_model import rom_f32  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')
OUT = os.path.join(ROOT, 'docs', 'vectors', 'c8_gpu.json')

SET_PROGRAM, SET_COMBINERS = 0x80040440, 0x80040EF0


def drop_repeats(p):
    """A breakpoint can stop twice at the same call without progress (C3,
    "Vorfälle"); the first of two such stops has no words of its own and the
    same site, put pointer and arguments as the second. Removes them and
    renumbers."""
    calls, out, dup = p['calls'], [], 0
    for i, c in enumerate(calls):
        n = calls[i + 1] if i + 1 < len(calls) else None
        if (n and not c.get('words') and n['site'] == c['site'] and n['put'] == c['put']
                and n['args'] == c['args'] and n['frame'] == c['frame']):
            dup += 1
            continue
        out.append(c)
    p['calls'] = out
    return dup


def words_of(c):
    if not c.get('words'):
        return []
    return list(struct.unpack(f"<{len(c['words']) // 8}I", bytes.fromhex(c['words'])))


def methods_of(c):
    return [x for x in nv2a.decode_pb(words_of(c)) if not isinstance(x[0], str)]


# ---------------------------------------------------------------- 1. stream

def match(D, T, j):
    i, n = 0, len(D)
    while i < n:
        if j >= len(T):
            return i, j, 'trace end'
        t = T[j]
        m, v = D[i][0], D[i][1]
        if m == nv2a.ARRAY_ELEMENT16:
            if 'ae16_words' not in t:
                return i, j, 'expected ARRAY_ELEMENT16 run'
            k = t['ae16_words']
            if any(i + q >= n or D[i + q][0] != nv2a.ARRAY_ELEMENT16 for q in range(k)):
                return i, j, 'ARRAY_ELEMENT16 run shorter than in the trace'
            i += k
            j += 1
            continue
        if 'v' in t and t['m'] == m and t['v'] == v:
            i += 1
            j += 1
            continue
        if (m == nv2a.BEGIN_END and v == 0 and i + 2 < n and D[i + 1][0] == nv2a.BEGIN_END
                and D[i + 2][0] == 0x1810 and t['m'] == 0x1810):
            i += 2
            continue
        return i, j, f'differs: sent {m:#x}={v:#x}, trace {t}'
    return i, j, 'ok'


def check_stream(p, T):
    out = []
    for fi, f in enumerate(p['frames']):
        cs = [c for c in p['calls'] if c['frame'] == fi]
        if not cs:
            continue
        D = [x for c in cs for x in methods_of(c)]
        best = None
        for j in range(len(T)):
            t = T[j]
            if 'v' in t and t['m'] == D[0][0] and t['v'] == D[0][1]:
                i, jj, why = match(D[:50], T, j)
                if why != 'ok':
                    continue
                i, jj, why = match(D, T, j)
                if best is None or i > best[0]:
                    best = (i, j, jj, why)
                if why == 'ok':
                    break
        rec = {'frame': fi, 't_prev': f['t_prev'], 'calls': len(cs), 'methods': len(D)}
        if best is None:
            rec['result'] = 'no start found'
        else:
            rec.update({'matched': best[0], 'trace_from': T[best[1]]['pos'],
                        'result': best[3]})
        out.append(rec)
        print(f"stream frame {fi} t_prev={f['t_prev']:.3f}: {rec['result']} "
              f"({rec.get('matched')}/{len(D)})", flush=True)
    return out


# ---------------------------------------------------------------- 2. textures

def texture_cases(seed_fx):
    """Field, constructor site, model; in the order the setup makes them."""
    cube_face = lambda n: (n * n * 4 + 127) & ~127  # noqa: E731

    def cube(n):
        return b''.join(f + bytes(cube_face(n) - len(f)) for f in tm.normal_cube(n))

    noise = tm.fractal_multi(3, 256, 0x1A9, seed_fx, 0, 0x55)[0]
    return [
        ('anim+0x938', 0x800610F6, 'texgen_normal_cube(128)', lambda: cube(128)),
        ('anim+0x934', 0x800610F6, 'texgen_normal_cube(64)', lambda: cube(64)),
        ('anim+0x940', 0x800609EC, 'texgen_fractal(128, normals, 1/512, 0x200, 0, 0xFFFFFF, -, drawn, 0xFF, 50)',
         lambda: tm.fractal(128, 1, rom_f32(0x800128E4), 0x200, 0, 0xFFFFFF, 0xFF, 0, 0xFF, 50)),
        ('anim+0x944', 0x8006062E, 'texgen_falloff_mips(256, 256, 0xFFFFFFFF, 0, 0x3039)',
         lambda: b''.join(tm.falloff_mips(256, 256, 0, 0x3039))),
        ('logo+0x10', 0x800607C1, 'texgen_gradient(16, 128, 0xFF000100, 0xFF4B9B4B)',
         lambda: tm.gradient(16, 128, 0xFF000100, 0xFF4B9B4B)),
        ('logo+0x24', 0x800603F5, 'texgen_radial(256, 6, 0, 0.5, 0.5)',
         lambda: tm.radial(256, 6, 0, 0.5, 0.5)),
        ('logo+0x38', 0x800607C1, 'texgen_gradient(16, 128, 0xFF000000, 0xFFFFFFFF)',
         lambda: tm.gradient(16, 128, 0xFF000000, 0xFFFFFFFF)),
        ('logo+0xC0', 0x8005BA95, 'texels 0x80073FD0 (Logo.md 2.4)', lambda: tm_quad()),
        ('fx+0x08', 0x80060E17, 'texgen_fractal_multi layer copy 1', lambda: noise[0]),
        ('fx+0x0C', 0x80060E17, 'texgen_fractal_multi layer copy 2', lambda: noise[1]),
        ('fx+0x10', 0x80060E17, 'texgen_fractal_multi layer copy 3', lambda: noise[2]),
    ]


def tm_quad():
    img = open(os.path.join(ROOT, 'out', 'rom', 'kernel.img'), 'rb').read()
    raw = img[0x80073FD0 - 0x80010000:0x80073FD0 - 0x80010000 + 0x400]
    return tm.swizzle(raw, 16, 16, 4)


def fx_seed():
    """The setup draws the seed of the noise from the shared generator
    (0x8005A14F); it is its 38th step from 0x76543210 (Timing.md 4.1)."""
    r = tm.Rand(0x76543210)
    for _ in range(38):
        s = r.step()
    return s


def check_textures(p):
    created = {}
    for ci, c in enumerate(p['calls']):
        if c.get('res'):
            created.setdefault(int(c['site'], 16), []).append((ci, c['res']))
    data = {t['res']: t for t in p['textures']}
    seed = fx_seed()
    out = []
    used = {}
    for field, site, what, model in texture_cases(seed):
        k = used.get(site, 0)
        used[site] = k + 1
        ci, res = created[site][k]
        t = data[res]
        head = struct.unpack('<5I', bytes.fromhex(t['head']))
        got = bytes.fromhex(t['data'])
        want = model()
        diff = sum(1 for a, b in zip(got, want) if a != b) + abs(len(got) - len(want))
        out.append({'field': field, 'site': f'{site:#x}', 'generator': what, 'resource': res,
                    'phys': f'{head[1]:#x}', 'format': f'{head[3]:#010x}', 'bytes': len(got),
                    'sha256': hashlib.sha256(got).hexdigest(), 'differ': diff})
        print(f'texture {field:11s} {what}: {len(got)} bytes, {diff} differ', flush=True)
    return {'fx_seed': f'{seed:#010x}', 'cases': out}


# ---------------------------------------------------------------- 3. GPU objects

def check_objects(p):
    gpu = json.load(open(os.path.join(ROOT, 'data', 'gpu', 'gpu.json')))
    progs = {tuple(int(x, 16) for ins in pr['words'] for x in ins): pr['addr']
             for pr in gpu['programs']}
    combs = {tuple((int(m, 16), int(v, 16)) for m, v in cb['methods']): cb['addr']
             for cb in gpu['combiners']}
    seen_p, seen_c, bad = {}, {}, []
    for ci, c in enumerate(p['calls']):
        tgt = int(c['target'], 16)
        if tgt == SET_PROGRAM and int(c['args'][0], 16) & 1:
            ws = tuple(v for m, v, *_ in methods_of(c) if 0xB00 <= m < 0xB80)
            # a program longer than 32 instructions comes in several chunks
            hit = progs.get(ws)
            if hit is None:
                bad.append({'call': ci, 'kind': 'program', 'words': len(ws)})
            else:
                seen_p.setdefault(hit, []).append(c['site'])
        elif tgt == SET_COMBINERS and int(c['args'][0], 16):
            ms = [(m, v) for m, v, *_ in methods_of(c)]
            hit = None
            for key, addr in combs.items():
                k = len(key)
                for s in range(len(ms) - k + 1):
                    if tuple(ms[s:s + k]) == key:
                        hit = addr
                        break
                if hit:
                    break
            if hit is None:
                bad.append({'call': ci, 'kind': 'combiners'})
            else:
                seen_c.setdefault(hit, []).append(c['site'])
    summ = lambda d: {a: {'calls': len(s), 'sites': sorted(set(s))} for a, s in d.items()}  # noqa: E731
    print(f'objects: {sum(len(s) for s in seen_p.values())} program loads, '
          f'{sum(len(s) for s in seen_c.values())} combiner loads, {len(bad)} unmatched')
    return {'programs': summ(seen_p), 'combiners': summ(seen_c), 'unmatched': bad}


# ---------------------------------------------------------------- 4. draws

# Step of a draw: (phase, context, site). The context of a call is the
# function that holds its call site, except for prim_group_draw, which
# takes the context of the last call outside it (its caller: chamber_draw,
# chamber_light_view, fx_render_offscreen's depth pass). The phase is
# 'setup' in frame 0, then 'update' until anim_draw's first call
# (0x80056D2A) and 'draw' from there on.
PRIM_GROUP_DRAW = 0x8005EE08
ANIM_DRAW_FIRST = 0x80056D2A
STEPS = {
    ('setup', 'device_setup', 0x80057B7E): ('A1', 'Setup: finish the frame'),
    ('setup', 'chamber_draw', 'prim'): ('A2', 'Cube texture: chamber into one face'),
    ('update', 'chamber_light_view', 'prim'): ('U1', 'Light view: chamber into depth'),
    ('update', 'fx_render_offscreen', 0x8005A373): ('U2', 'Veil: quad into the target texture'),
    ('update', 'fx_render_offscreen', 'prim'): ('U3', 'Veil: chamber into depth'),
    ('update', 'logo_draw', 0x8005BF06): ('U4', 'Logo into the texture: mesh A'),
    ('update', 'logo_draw', 0x8005BF30): ('U4', 'Logo into the texture: mesh B'),
    ('update', 'logo_draw', 0x8005BF5A): ('U4', 'Logo into the texture: mesh C'),
    ('update', 'logo_draw', 0x8005C17C): ('U4', 'Logo into the texture: mesh D'),
    ('update', 'logo_draw', 0x8005C28E): ('U4', 'Logo into the texture: Q1'),
    ('update', 'logo_draw', 0x8005C2B5): ('U4', 'Logo into the texture: Q2'),
    ('update', 'logo_draw', 0x8005C347): ('U4', 'Logo into the texture: lettering E'),
    ('draw', 'chamber_draw', 'prim'): ('D1', 'Chamber'),
    ('draw', 'blob_draw', 0x80058702): ('D2', 'Blob: glow surface'),
    ('draw', 'blob_draw', 0x80058953): ('D2', 'Blob: mesh'),
    ('draw', 'blob_draw', 0x80058ADA): ('D2', 'Blob: drop'),
    ('draw', 'logo_draw', 0x8005BDAF): ('D3', 'Logo as image'),
    ('draw', 'logo_draw', 0x8005BF06): ('D3', 'Logo: mesh A'),
    ('draw', 'logo_draw', 0x8005BF30): ('D3', 'Logo: mesh B'),
    ('draw', 'logo_draw', 0x8005BF5A): ('D3', 'Logo: mesh C'),
    ('draw', 'logo_draw', 0x8005C17C): ('D3', 'Logo: mesh D'),
    ('draw', 'logo_draw', 0x8005C28E): ('D3', 'Logo: Q1'),
    ('draw', 'logo_draw', 0x8005C2B5): ('D3', 'Logo: Q2'),
    ('draw', 'logo_draw', 0x8005C347): ('D3', 'Logo: lettering E'),
    ('draw', 'fx_draw', 0x80059EFE): ('D4', 'Veil'),
    ('draw', 'fx_draw', 0x8005A100): ('D4', 'Glow over the veil'),
    ('draw', 'anim_draw', 0x80056DF9): ('D5', 'Finish the frame'),
}

SURFACE_LABELS = {
    0x800610F6: ['normal_cube128', 'normal_cube64'], 0x800609EC: ['fractal128'],
    0x8006062E: ['falloff256'], 0x8005FD43: ['light_view_a'], 0x8005FD57: ['light_view_b'],
    0x800607C1: ['gradient_green', 'gradient_grey'], 0x800603F5: ['radial256'],
    0x8005BA95: ['tm16'], 0x8005A445: ['fx_target_14'], 0x8005A45B: ['fx_target_18'],
    0x8005A471: ['fx_depth'], 0x80060E17: ['noise_a', 'noise_b', 'noise_c'],
    0x80061515: ['chamber_cube256'], 0x8006153D: ['cube_depth'], 0x8005C3EE: ['logo_target'],
}


def owners():
    import bisect
    import re
    g = json.load(open(os.path.join(ROOT, 'out', 'ghidra', 'kernel_graph.json')))
    ents = sorted(int(f['entry'], 16) for f in g)
    names = {}
    for line in open(os.path.join(ROOT, 'docs', 'decomp', 'README.md'), encoding='utf-8'):
        m = re.match(r'\| (0x[0-9A-F]{8}) \| `([^`]+)`', line)
        if m:
            names[int(m.group(1), 16)] = m.group(2)

    def owner(a):
        e = ents[bisect.bisect_right(ents, a) - 1]
        return e, names.get(e, f'{e:#x}')
    return owner


def surface_names(p):
    """Physical address -> name for the textures the run saw being made.
    A cube texture is named at its first face only."""
    heads = {t['res']: struct.unpack('<5I', bytes.fromhex(t['head'])) for t in p['textures']}
    names, used = {}, {}
    for c in p['calls']:
        site = int(c['site'], 16)
        if c.get('res') in heads and site in SURFACE_LABELS:
            k = used.get(site, 0)
            used[site] = k + 1
            lab = SURFACE_LABELS[site]
            names[heads[c['res']][1]] = lab[min(k, len(lab) - 1)]
    return names


def state_methods():
    """The registers a draw snapshot keeps: blend, alpha test, depth, cull,
    colour mask, combiners, texture stages, transform mode."""
    ms = set()
    ms.update([0x300, 0x304, 0x308, 0x30C, 0x33C, 0x340, 0x344, 0x348, 0x34C, 0x350,
               0x354, 0x358, 0x35C, 0x39C, 0x3A0, 0x288, 0x28C, 0x17F8, 0x1E20, 0x1E24,
               0x1E60, 0x1E70, 0x1E74, 0x1E78, 0x1E94, 0x1EA0, 0x1D84, 0x1E6C])
    ms.update(range(0x260, 0x280, 4))
    ms.update(range(0xA60, 0xAE0, 4))
    ms.update(range(0x1E40, 0x1E60, 4))
    for st in range(4):
        ms.update(0x1B00 + 0x40 * st + k for k in range(0, 0x28, 4))
    return ms


STATE = state_methods()


def draw_t(f):
    if 'draw' not in f:
        return None
    return struct.unpack_from('<f', bytes.fromhex(f['draw']['obj']), 0x10)[0]


def check_draws(p):
    owner = owners()
    gpu = json.load(open(os.path.join(ROOT, 'data', 'gpu', 'gpu.json')))
    progs = {tuple(int(x, 16) for ins in pr['words'] for x in ins): pr['addr']
             for pr in gpu['programs']}
    combs = {tuple((int(m, 16), int(v, 16)) for m, v in cb['methods']): cb['addr']
             for cb in gpu['combiners']}
    comb_lens = {len(k) for k in combs}
    snames = surface_names(p)
    handle_prog, handle_comb = {}, {}
    by_frame = {}
    for c in p['calls']:
        by_frame.setdefault(c['frame'], []).append(c)
    frames, unmapped = [], 0
    kinds, kind_index = [], {}
    prev_fi, R = None, {}
    state = {}
    for fi, cs in sorted(by_frame.items()):
        f = p['frames'][fi]
        phase = 'setup' if fi == 0 else 'update'
        context = None
        if prev_fi is None or fi != prev_fi + 1:
            # Not the frame after the last one recorded: nothing is known
            # about the state before the first call.
            R = {}
            state = {'program': None, 'combiners': None, 'tex': {}, 'color': None, 'zeta': None}
            pwords, pairs = None, []
        prev_fi = fi
        draws = []
        for c in cs:
            site, tgt = int(c['site'], 16), int(c['target'], 16)
            if site == ANIM_DRAW_FIRST:
                phase = 'draw'
            ent, oname = owner(site)
            in_prim = ent == PRIM_GROUP_DRAW
            if not in_prim:
                context = oname
            ms = methods_of(c)
            if tgt == SET_PROGRAM:
                h = int(c['args'][0], 16)
                if h & 1:
                    ws = tuple(v for m, v, *_ in ms if 0xB00 <= m < 0xB80)
                    if ws in progs:
                        handle_prog[h] = progs[ws]
                    state['program'] = handle_prog.get(h, f'handle {h:#x}')
                else:
                    state['program'] = f'fixed function {h:#x}' if h else 'none'
            elif tgt == SET_COMBINERS:
                h = int(c['args'][0], 16)
                if h:
                    pairs = [(m, v) for m, v, *_ in ms]
                    for key, addr in combs.items():
                        k = len(key)
                        if any(tuple(pairs[s:s + k]) == key for s in range(len(pairs) - k + 1)):
                            handle_comb[h] = addr
                    state['combiners'] = handle_comb.get(h, f'handle {h:#x}')
                else:
                    state['combiners'] = 'from texture stages'
            prim = None
            for m, v, *_ in ms:
                # Program and combiner loads are also recognised in the
                # stream itself: in the setup the probe does not stop at
                # prim_group_draw's calls of 0x80040440 and 0x80040EF0, and
                # their words land in the previous call (probe_c8.py).
                if m == 0x1E9C:
                    pwords = []
                elif 0xB00 <= m < 0xB80 and pwords is not None:
                    pwords.append(v)
                elif m == 0x1EA0 and pwords:
                    state['program'] = progs.get(tuple(pwords), state['program'])
                    pwords = None
                if m == 0x260:
                    pairs = []
                if len(pairs) < 64:
                    pairs.append((m, v))
                    if len(pairs) in comb_lens:
                        state['combiners'] = combs.get(tuple(pairs), state['combiners'])
                if m in STATE:
                    R[m] = v
                if m == 0x210:
                    state['color'] = snames.get(v, f'{v:#x}')
                elif m == 0x214:
                    state['zeta'] = snames.get(v, f'{v:#x}')
                elif 0x1B00 <= m < 0x1C00 and (m - 0x1B00) % 0x40 == 0:
                    state['tex'][(m - 0x1B00) // 0x40] = snames.get(v, f'{v:#x}')
                elif 0x1B0C <= m < 0x1C00 and (m - 0x1B0C) % 0x40 == 0 and not v & (1 << 30):
                    state['tex'].pop((m - 0x1B0C) // 0x40, None)
                elif m == nv2a.BEGIN_END:
                    if v:
                        prim = {'prim': v, 'count': 0}
                    elif prim is not None:
                        key = (phase, context, 'prim' if in_prim else site)
                        step = STEPS.get(key)
                        unmapped += step is None
                        snap = tuple(sorted(R.items()))
                        kk = (step[0] if step else None, f'{site:#x}', snap)
                        if kk not in kind_index:
                            kind_index[kk] = len(kinds)
                            kinds.append({'step': kk[0], 'what': step[1] if step else None,
                                          'site': kk[1], 'frames': [], 'draws': 0,
                                          'state': {f'{m:#06x}': f'{v:#010x}' for m, v in snap}})
                        kd = kinds[kind_index[kk]]
                        kd['draws'] += 1
                        if fi not in kd['frames']:
                            kd['frames'].append(fi)
                        draws.append({'kind': kind_index[kk],
                                      'site': f'{site:#x}', 'context': context, 'phase': phase,
                                      'step': step[0] if step else None,
                                      'what': step[1] if step else None,
                                      'prim': prim['prim'], 'count': prim['count'],
                                      'program': state['program'],
                                      'combiners': state['combiners'],
                                      'textures': {str(k): v for k, v in sorted(state['tex'].items())},
                                      'color': state['color'], 'zeta': state['zeta']})
                        prim = None
                elif prim is not None and m in (0x1800, 0x1808, 0x1810, 0x1818):
                    prim['count'] += {0x1800: 2, 0x1808: 1, 0x1818: 1}.get(m, 0)
                    if m == 0x1810:
                        prim['count'] += (v >> 24) + 1
        frames.append({'frame': fi, 't_prev': f['t_prev'], 't': draw_t(f), 'draws': draws})
        print(f'draws frame {fi}: {len(draws)} draws, '
              f'{sum(1 for d in draws if d["step"] is None)} without step', flush=True)
    return {'unmapped': unmapped, 'kinds': kinds, 'frames': frames}


# ---------------------------------------------------------------- 5. effects

def fbits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def constants_sent(c):
    """(first constant, words) of a call of 0x80040520."""
    base, words = None, []
    for m, v, *_ in methods_of(c):
        if m == 0x1EA4:
            base = v
        elif 0xB80 <= m < 0xC00:
            words.append(v)
    return base, words


def check_effects(p):
    import chamber_model as cm
    import effects_model as em
    cp = cm.CamPath()
    proj = cm.projection()
    out, ok, bad = [], 0, 0
    for fi, f in enumerate(p['frames']):
        if 'draw' not in f:
            continue
        obj = bytes.fromhex(f['draw']['obj'])
        cam = bytes.fromhex(f['draw']['cam'])
        g = lambda b, o: struct.unpack_from('<f', b, o)[0]  # noqa: E731
        t, x20 = g(obj, 0x10), g(obj, 0x20)
        D, az, el, aspect = g(cam, 0x18), g(cam, 0x08), g(cam, 0x10), g(cam, 0xA4)
        view = cm.camera(cp, t)[2]
        vp = cm.m_mul(view, proj)
        quad, mat, depth = em.offscreen_constants(D, aspect, vp)
        c0, c1 = em.veil_colours(t, x20)
        tex = em.veil_texcoords(D, aspect, vp, az, el)
        gl = em.glow(t, D)
        want = {
            0x8005A33F: (0x70, [fbits(x) for x in quad]),
            0x8005A35E: (0x60, [fbits(x) for x in mat]),
            0x8005A3C5: (0x70, [fbits(x) for x in depth]),
            0x80059EE8: (0x60, [fbits(x) for x in tex]),
        }
        # anim_object + 0x690, + 0x698, + 0x6A0, + 0x6A8 as float32 bit patterns
        # (Camera.md 6), for the core's host tests
        rec = {'frame': fi, 't': t, 'x20': x20, 'D': D,
               'track': [f'{fbits(g(cam, o)):#010x}' for o in (0x00, 0x08, 0x10, 0x18)],
               'checks': {}}
        for c in p['calls']:
            if c['frame'] != fi:
                continue
            site = int(c['site'], 16)
            if site in want:
                got = constants_sent(c)
                eq = got == want[site]
                rec['checks'][f'{site:#x} constants'] = eq
            elif site == 0x80059DF0:          # 0x800410B0, two colours
                ms = {m: v for m, v, *_ in methods_of(c)}
                eq = (ms.get(0xA68) == em.pack_colour(c0) and ms.get(0xA70) == em.pack_colour(c1)
                      and [ms.get(0x181C), ms.get(0x1820), ms.get(0x1824)] == [fbits(x) for x in c0[:3]])
                rec['checks']['0x80059df0 colours'] = eq
                rec['colours'] = [f'{em.pack_colour(c0):#010x}', f'{em.pack_colour(c1):#010x}']
            elif site == 0x80059FF9:          # 0x8003CED0, glow factor
                ms = {m: v for m, v, *_ in methods_of(c)}
                eq = gl is not None and ms.get(0xA60) == gl['factor']
                rec['checks']['0x80059ff9 glow factor'] = eq
            elif site == 0x8005A100:          # glow draw, lazy matrices
                ms = {}
                for m, v, *_ in methods_of(c):
                    ms.setdefault(m, v)
                eq = (gl is not None and ms.get(0x480) == fbits(gl['inv_s'])
                      and ms.get(0x494) == fbits(gl['inv_s']))
                rec['checks']['0x8005a100 glow matrix'] = eq
        if gl is None and any(int(c['site'], 16) == 0x8005A100 for c in p['calls'] if c['frame'] == fi):
            rec['checks']['glow drawn although model says not'] = False
        rec['glow'] = gl
        for v in rec['checks'].values():
            ok += v
            bad += not v
        out.append(rec)
        print(f'effects frame {fi} t={t:.3f}: ' + ', '.join(f'{k}={v}' for k, v in rec['checks'].items()),
              flush=True)
    return {'equal': ok, 'different': bad, 'frames': out}


# ---------------------------------------------------------------- 6. timeline

def timeline(p):
    """Frame i (i >= 1) is the i-th stop at 0x80057949; the probe reads t
    before it (t_prev). The step of frame i is t_prev[i + 1] - t_prev[i],
    a whole number of milliseconds (Timing.md 2); f32(t_prev + f32(ms *
    f32(0.001))) must give t_prev[i + 1] again, else the frame counts as
    not replayable. The last frame has no t after it and is left out.

    A breakpoint can stop twice at the same call without progress (C3,
    "Vorfaelle"; drop_repeats above does the same for the calls). Such a
    stop is no frame: the next stop has the same t and the same pushbuffer
    pointer, although every frame writes at least a clear and the
    presentation. Those stops are listed in repeated_stops; a replay skips
    them."""
    import numpy as np
    fr = p['frames']
    ms, after, repeats, bad = [], [], [], 0
    for i in range(1, len(fr) - 1):
        a, b = np.float32(fr[i]['t_prev']), np.float32(fr[i + 1]['t_prev'])
        d = int(round((float(b) - float(a)) * 1000))
        back = np.float32(np.float64(a) + np.float64(np.float32(np.float64(d) * np.float64(np.float32(0.001)))))
        bad += back != b
        if a == b and fr[i]['put'] == fr[i + 1]['put']:
            repeats.append(i)
        ms.append(d)
        after.append(f'{fbits(float(b)):#010x}')
    zero = [i + 1 for i, d in enumerate(ms) if d == 0]
    print(f'timeline: {len(ms)} stops, {bad} not replayable from whole milliseconds, '
          f'repeated stops {repeats}, steps of 0 ms {zero}')
    return {'first_frame': 1, 'not_replayable': int(bad), 'repeated_stops': repeats, 'ms': ms,
            't_after': after}


def main():
    run = sys.argv[1] if len(sys.argv) > 1 else 'c8_main2'
    d = os.path.join(RUNS, run)
    p = json.load(open(os.path.join(d, 'probe_c8.json')))
    dup = drop_repeats(p)
    T = nv2a.read_trace(open(os.path.join(d, 'trace.log'), 'rb').read())
    res = {'run': run,
           'command': f'python tools/xemu/probe_c8.py --name {run}',
           'repeated_stops_dropped': dup,
           'stream': check_stream(p, T),
           'textures': check_textures(p),
           'objects': check_objects(p),
           'draws': check_draws(p),
           'effects': check_effects(p),
           'timeline': timeline(p)}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w', newline='\n') as f:
        json.dump(res, f, indent=1)
        f.write('\n')
    print('->', OUT)


if __name__ == '__main__':
    main()
