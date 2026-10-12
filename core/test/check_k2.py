"""K2 checks that need Python: constants, the guest's memory, the executable specification.

    python core/test/check_k2.py out/core/native/test_k2.exe

1. Every constant `#define K_* <value>f /* <address> */` in
   core/src/Overview.c, Camera.c and Chamber.c is compared with the
   float32 at that address in out/rom/kernel.img (file offset = address -
   0x80010000, Overview.md); data/overview/asin_factors.bin with its range.
2. Geometry of all twelve groups (`test_k2 --mesh`) value for value
   against the model tools/xemu/chamber_model.py: positions, attributes,
   indices, form tables. Vertices the original counts but never writes
   must be 0 in the core. Where the recorded run is present
   (out/xemu/runs/c5_main/probe_c5.json, local only), the written vertices
   are compared with the guest's buffers as well; this covers the four
   groups whose hash in the vector file includes left-over memory.
3. Camera and chamber over the whole time line (`test_k2 --dump`) against
   the model: the keys of the path with tangents, frame, origin, look and
   fade; the camera at every millisecond from 0 to 8 s (path time,
   section, both switches, eye, target, view); track state and the world
   matrix of every part every 25 ms and one float32 step on either side
   of each knot of the tracks and each switch of detail.
4. A measurement for the gap closed in K2: how
   often the sum order the model had for 0x8005667A before K2 changes a
   value, over the same camera times and the geometry.
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
import chamber_model as M  # noqa: E402

KERNEL = os.path.join(ROOT, 'out', 'rom', 'kernel.img')
PROBE = os.path.join(ROOT, 'out', 'xemu', 'runs', 'c5_main', 'probe_c5.json')
BASE = 0x80010000
TMP = os.path.join(ROOT, 'out', 'core', 'k2')
NAN = 0x7FC00000

checks = failed = 0


def check(ok, what):
    global checks, failed
    checks += 1
    failed += not ok
    print(('ok    ' if ok else 'FAIL  ') + what)


def fb(x):
    """float32 bits, NaN as one pattern"""
    x = float(x)
    return NAN if x != x else struct.unpack('<I', struct.pack('<f', x))[0]


def bf(u):
    return struct.unpack('<f', struct.pack('<I', u))[0]


def constants():
    img = open(KERNEL, 'rb').read()
    total, bad = 0, []
    for name in ('Overview.c', 'Camera.c', 'Chamber.c'):
        src = open(os.path.join(ROOT, 'core', 'src', name), encoding='utf-8').read()
        found = re.findall(r'#define\s+(K_\w+)\s+(-?[0-9.]+(?:e[-+]?[0-9]+)?)f\s+/\*\s*(0x[0-9A-Fa-f]{8})\s*\*/',
                           src)
        for cname, value, addr in found:
            a = int(addr, 16)
            rom = struct.unpack_from('<f', img, a - BASE)[0]
            if fb(rom) != fb(np.float32(float(value))):
                bad.append(f'{name} {cname} {value} at {addr}: kernel {rom!r}')
        total += len(found)
    for b in bad:
        print('      ' + b)
    check(total >= 80 and not bad, f'{total} constants of Overview.c, Camera.c, Chamber.c equal the kernel image, '
                                   f'{len(bad)} differ')
    tab = open(os.path.join(ROOT, 'data', 'overview', 'asin_factors.bin'), 'rb').read()
    check(tab == img[0x80066030 - BASE:0x800660F4 - BASE],
          'data/overview/asin_factors.bin equals 0x80066030-0x800660F3 of the kernel image')


def geometry(exe):
    os.makedirs(TMP, exist_ok=True)
    subprocess.run([exe, os.path.join(ROOT, 'out', 'pack', 'xboot.pak'), '--mesh', TMP], check=True)
    scene = M.load_scene()
    probe = json.load(open(PROBE))['static']['groups'] if os.path.exists(PROBE) else None
    values = guest_values = 0
    for lod in (0, 1):
        for g in range(6):
            grp = scene['groups'][g]
            m, info = M.build_group(grp, lod)
            stride = 11 if grp['vertex_format'] == 1 else 9
            rd = lambda ext: open(os.path.join(TMP, f'g{g}_d{lod}.{ext}'), 'rb').read()  # noqa: E731
            P, A, I, F = rd('pos'), rd('attr'), rd('idx'), rd('forms')
            nv = len(P) // 12
            cp = struct.unpack('<%dI' % (3 * nv), P)
            ca = struct.unpack('<%dI' % (stride * nv), A)
            bad, zero_bad, unwritten = 0, 0, 0
            for v in range(nv):
                if m.pos[v] is None:
                    unwritten += 1
                    zero_bad += any(cp[3 * v:3 * v + 3]) or any(ca[stride * v:stride * (v + 1)])
                    continue
                want = [fb(x) for x in m.pos[v]] + [fb(x) for x in m.attr[v]]
                bad += want != list(cp[3 * v:3 * v + 3]) + list(ca[stride * v:stride * (v + 1)])
                values += 3 + stride
            idx_ok = list(struct.unpack('<%dH' % (len(I) // 2), I)) == m.idx
            forms = [list(struct.unpack_from('<6I', F, 24 * k)) for k in range(len(F) // 24)]
            forms_ok = forms == [[x['v0'], x['nv'], x['i0'], x['prims'], x['ni'], x['caps']] for x in info]
            check(len(m.pos) == nv and bad == 0 and zero_bad == 0 and idx_ok and forms_ok,
                  f'type {g} detail {lod}: {nv} vertices ({unwritten} not written, 0 in the core: '
                  f'{zero_bad == 0}), {len(I) // 2} indices, forms equal the model; {bad} vertices differ')
            if probe is not None:
                G = probe[6 * lod + g]
                gp = bytes.fromhex(G['geometry']['positions'])
                ga = bytes.fromhex(G['geometry']['attributes'])
                gbad = 0
                for v in range(nv):
                    if m.pos[v] is None:
                        continue
                    gbad += (P[12 * v:12 * v + 12] != gp[12 * v:12 * v + 12] or
                             A[4 * stride * v:4 * stride * (v + 1)] != ga[4 * stride * v:4 * stride * (v + 1)])
                    guest_values += 3 + stride
                check(gbad == 0, f'type {g} detail {lod}: written vertices equal the guest\'s memory '
                                 f'(c5_main), {gbad} differ')
    print(f'      {values} vertex values against the model'
          + (f', {guest_values} against the guest' if probe else ', recorded run not present'))


def f32(x):
    return float(np.float32(x))


def times():
    """(t, with chamber) for the dump"""
    out = {}
    for ms in range(0, 8001):
        out[fb(f32(ms / 1000.0))] = False
    for ms in range(0, 8001, 25):
        out[fb(f32(ms / 1000.0))] = True
    knots = [f32((M.c(0x3F19999A) + M.c(0x3E800000)) + i * M.c(0x40900000) / 28.0) for i in range(29)]
    for x in knots + [5.2, 5.35, 5.936, 6.0]:
        u = fb(f32(x))
        for d in (-1, 0, 1):
            out[u + d] = True
    return sorted(out.items())


def camera_and_chamber(exe):
    ts = times()
    path = os.path.join(TMP, 'times.txt')
    with open(path, 'w') as f:
        for u, w in ts:
            f.write('%08X %s\n' % (u, 'cw' if w else 'c'))
    lines = subprocess.run([exe, os.path.join(ROOT, 'out', 'pack', 'xboot.pak'), '--dump', path],
                           capture_output=True, text=True, check=True).stdout.split('\n')
    cp, ch = M.CamPath(), M.Chamber()
    pos = 0
    keys = []
    while lines[pos].startswith('k'):
        keys.append([int(x, 16) for x in lines[pos].split()[1:]])
        pos += 1
    want = [[fb(k['t'])] + [fb(x) for x in k['eye'] + k['eye_tan'] + k['target'] + k['target_tan']]
            + [fb(k['cut']), fb(k['split'])] for k in cp.keys]
    check(keys == want, f'path {cp.path}: {len(keys)} keys with tangents equal the model\'s')
    fl = lines[pos].split()
    pos += 1
    fw = [cp.path, cp.first, cp.n_table] + [fb(x) for x in cp.frame + cp.origin + cp.look] + \
         [fb(cp.fade_t0), fb(cp.fade_k)]
    check([int(fl[1]), int(fl[2]), int(fl[3])] + [int(x, 16) for x in fl[4:]] == fw,
          'path, first key, frame, origin, look and fade equal the model\'s')

    cam_bad = pose_bad = world_bad = n_cam = n_ch = nan_frames = 0
    shown = 0
    for u, w in ts:
        t = bf(u)
        c = lines[pos].split()
        pos += 1
        eye, tgt, view, info = M.camera(cp, t)
        key = info['key'] if info['key'] is not None else -1
        n_keys = len(cp.keys)
        on_d = int(key >= 1 and key < n_keys - 1)
        on_e = int(key == -1 or key > cp.n_table - 1)
        want = [fb(info['s']), key, on_d, on_e] + [fb(x) for x in eye + tgt + view]
        got = [int(c[1], 16), int(c[2]), int(c[3]), int(c[4])] + [int(x, 16) for x in c[5:]]
        n_cam += 1
        if got != want:
            cam_bad += 1
            if shown < 3:
                shown += 1
                print(f'      camera t={t!r}: core {c[1:6]}, model {want[:5]}')
        if not w:
            continue
        n_ch += 1
        p = lines[pos].split()
        pos += 1
        st = ch.update(t)
        pose_bad += [int(p[1], 16), int(p[2], 16), int(p[3])] != [fb(st['u']), fb(st['k']), st['lod']]
        W = [m for grp in ch.world(st) for m in grp]
        any_nan = False
        for m in W:
            got = [int(x, 16) for x in lines[pos].split()[1:]]
            pos += 1
            world_bad += got != [fb(x) for x in m]
            any_nan |= NAN in got
        nan_frames += any_nan
    check(cam_bad == 0, f'camera at {n_cam} times from 0 to 8 s: {cam_bad} differ from the model '
                        f'(path time, section, switches, eye, target, view)')
    check(pose_bad == 0 and world_bad == 0,
          f'chamber at {n_ch} times: {pose_bad} track states and {world_bad} of {271 * n_ch} world matrices '
          f'differ from the model; {nan_frames} times with NaN parts')
    return [bf(u) for u, _ in ts]


def old_order(ts):
    """The sum order the model had for 0x8005667A before K2."""
    new = M.v_mul_m3

    def old(v, m):
        return [M.f32(v[0] * m[0] + m[4] * v[1] + m[8] * v[2]),
                M.f32(m[5] * v[1] + m[9] * v[2] + m[1] * v[0]),
                M.f32(m[6] * v[1] + m[10] * v[2] + m[2] * v[0])]
    res = {}
    for name, fn in (('new', new), ('old', old)):
        M.v_mul_m3 = fn
        cp = M.CamPath()
        res[name] = ([M.camera(cp, t)[2] for t in ts],
                     [M.build_group(M.load_scene()['groups'][5], lod)[0].pos for lod in (0, 1)])
    M.v_mul_m3 = new
    views = sum(a != b for a, b in zip(res['new'][0], res['old'][0]))
    verts = sum(a != b for la, lb in zip(res['new'][1], res['old'][1]) for a, b in zip(la, lb))
    print(f'      sum order of 0x8005667A as the model had it before K2: {views} of {len(ts)} views '
          f'and {verts} of {sum(len(x) for x in res["new"][1])} lathe vertices change')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.splitlines()[2].strip())
    exe = os.path.abspath(sys.argv[1])
    constants()
    geometry(exe)
    ts = camera_and_chamber(exe)
    if '--no-order' not in sys.argv:
        old_order(ts)
    print(f'{checks} checks, {failed} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
