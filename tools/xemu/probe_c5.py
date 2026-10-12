"""Records the state of camera and chamber at chosen animation times.

    python tools/xemu/probe_c5.py --name c5_pose --t 0.75 --t 3.0 --t 5.6

Starts xemu like probe.py and stops at the entry of anim_draw (0x80056D0B),
where anim_update has finished for this frame. Each stop reads t
(0x8006F060). At the first stop with t >= a target the script reads

- the animation object with camera, chamber and camera path,
  0x8006F050-0x80070DB8, and the expanded path keys behind it,
- the per-frame results of chamber_update: the 29 track rotations
  (*0x8006FB9C, 0x740 bytes) and the 32 track positions (*0x8006FBA4,
  0x180 bytes),
- once, at the first capture, the static data that chamber_setup built:
  the twelve groups (pointers at 0x8006F9B8), their form and placement
  records, the decoded quaternions, positions and track index lists,

then arms two more breakpoints for the rest of this frame's draw call:
0x8005F0FE in prim_group_draw, where the world matrix W of one placed part
lies at ebp-0x98 and ebx/esi point to group and placement, and 0x80056D5A,
the return from chamber_draw. The capture ends there or at the next
anim_draw.

Output: out/xemu/runs/<name>/probe_c5.json
"""

import argparse
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
from gdbstub import GDB  # noqa: E402

ANIM_DRAW = 0x80056D0B
PART_W = 0x8005F0FE
CHAMBER_DONE = 0x80056D5A
T_ADDR = 0x8006F060
OBJ = (0x8006F050, 0x80070DB8 - 0x8006F050)
CHAMBER = 0x8006F9B8


def u32(b, o=0):
    return struct.unpack_from('<I', b, o)[0]


def read_static(g):
    s = {}
    ch = g.read(CHAMBER, 0x200)
    groups = []
    for i in range(12):
        p = u32(ch, i * 4)
        gb = g.read(p, 0x6C)
        n_forms, n_place = u32(gb, 0x10), u32(gb, 0x18)
        res = [g.read(u32(gb, o), 0x40) for o in (0, 4, 8)]
        forms = g.read(u32(gb, 0xC), n_forms * 0x18)
        n_vert = sum(u32(forms, k * 0x18 + 4) for k in range(n_forms))
        n_idx = sum(u32(forms, k * 0x18 + 0x10) for k in range(n_forms))
        stride = {0: 0x24, 1: 0x2C}.get(u32(gb, 0x60), 0)
        # vertex buffers: physical address in word 1, read through the
        # kernel's mapping at 0x80000000; index buffer: pointer in word 1
        geo = {'positions': g.read(0x80000000 | u32(res[0], 4), n_vert * 12).hex(),
               'attributes': g.read(0x80000000 | u32(res[1], 4), n_vert * stride).hex(),
               'indices': g.read(u32(res[2], 4), n_idx * 2).hex()}
        groups.append({
            'ptr': f'{p:#010x}', 'raw': gb.hex(), 'res': [r.hex() for r in res],
            'forms': forms.hex(), 'geometry': geo,
            'places': g.read(u32(gb, 0x14), n_place * 0x11C).hex()})
    s['groups'] = groups
    n_q, n_p = 0x19D, 0x140
    s['quats'] = g.read(u32(ch, 0x60), n_q * 16).hex()
    s['positions'] = g.read(u32(ch, 0xD8), n_p * 12).hex()
    s['rot_tracks'] = [g.read(u32(ch, 0x64 + 4 * i), 60).hex() for i in range(29)]
    s['pos_tracks'] = [g.read(u32(ch, 0xDC + 4 * i), 60).hex() for i in range(32)]
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--t', action='append', type=float, required=True)
    ap.add_argument('--timeout', type=float, default=600)
    a = ap.parse_args()
    targets = sorted(a.t)
    ns = argparse.Namespace(name=a.name, frame=[], frames=None, brk='gdb', video=False,
                            audio=False, duration=0, xemu='release', timeout=a.timeout,
                            keep_image=False, trace=[])
    b = bench.Bench(ns)
    b.start()
    caps, static, frames = [], None, 0
    try:
        g = GDB(bench.GDB_PORT)
        g.hbreak(ANIM_DRAW)
        armed = set([ANIM_DRAW])
        cur = None
        while targets or cur is not None:
            if time.monotonic() - b.t0 > a.timeout:
                b.log['notes'].append('timeout')
                break
            g.cont()
            if g.wait_stop(60) is None:
                b.log['notes'].append('no hit for 60 s, run ended')
                break
            r = g.regs()
            eip = r['eip']
            if eip == ANIM_DRAW:
                frames += 1
                if cur is not None:
                    caps.append(cur)
                    cur = None
                    for x in (PART_W, CHAMBER_DONE):
                        g.hbreak(x, False)
                        armed.discard(x)
                t = struct.unpack('<f', g.read(T_ADDR, 4))[0]
                if targets and t >= targets[0]:
                    while targets and t >= targets[0]:
                        hit_target = targets.pop(0)
                    if static is None:
                        static = read_static(g)
                    ch = g.read(CHAMBER, 0x200)
                    b.tr.update()
                    cur = {'target': hit_target, 't': t, 'frame_in_anim': frames,
                           'flips': b.tr.flips,
                           'obj': g.read(*OBJ).hex(),
                           'rot_mats': g.read(u32(ch, 0x1E4), 0x740).hex(),
                           'track_pos': g.read(u32(ch, 0x1EC), 0x180).hex(),
                           'parts': [], 'chamber_done': False}
                    for x in (PART_W, CHAMBER_DONE):
                        g.hbreak(x)
                        armed.add(x)
            elif eip == PART_W and cur is not None:
                cur['parts'].append({'group': f"{r['ebx']:#010x}", 'place': f"{r['esi']:#010x}",
                                     'W': g.read(r['ebp'] - 0x98, 64).hex()})
            elif eip == CHAMBER_DONE and cur is not None:
                cur['chamber_done'] = True
                caps.append(cur)
                cur = None
                for x in (PART_W, CHAMBER_DONE):
                    g.hbreak(x, False)
                    armed.discard(x)
            # Continuing on a breakpoint address hits it again at once; step over it.
            if eip in armed:
                g.hbreak(eip, False)
                g.req('s')
                g.hbreak(eip)
        for x in armed:
            g.hbreak(x, False)
        g.close()
    finally:
        b.quit()
    b.log['probe_c5'] = {'targets': a.t, 'captures': [c['t'] for c in caps]}
    with open(os.path.join(b.dir, 'probe_c5.json'), 'w') as f:
        json.dump({'static': static, 'captures': caps}, f)
    with open(os.path.join(b.dir, 'run.json'), 'w') as f:
        json.dump(b.log, f, indent=1)
    print(f'{len(caps)} captures at t = {[round(c["t"], 4) for c in caps]}, '
          f'parts {[len(c["parts"]) for c in caps]} -> {b.dir}')


if __name__ == '__main__':
    main()
