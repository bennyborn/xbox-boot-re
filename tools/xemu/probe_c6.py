"""Records the state of the glowing blob in every frame, and what it hands
the GPU at chosen animation times.

    python tools/xemu/probe_c6.py --name c6_main --t 0.7 --t 2.0 --t 3.5

Starts xemu like probe.py and stops at the entry of blob_update
(0x80059789) in every frame. Each stop reads the arguments t and dt from
the stack, t, x1c and x20 of the animation object (0x8006F060-0x8006F073)
and the whole blob object (0x8006FBAC, 0xA38 bytes) before the update.

At the first stop with t >= a target the script also arms, for the rest of
this frame:

- 0x80056D0B anim_draw: the deformed vertex buffer that blob_update has
  just written (resource *0x8006FBC0, physical address in word 1), and the
  animation object 0x8006F050-0x8006FBAC with the camera;
- at the entry of blob_draw (0x800583BF) the two constant breakpoints below,
  so that the chamber's calls before it do not stop;
- inside blob_draw: 0x80058592 (packed glow colour in eax), 0x800586ED
  (the four glow quad vertices behind the pointer at ebp+0x70), and each call
  of 0x80040520 and 0x800410B0 whose return address lies in blob_draw
  (constant index, pointer, count; the vectors behind the pointer);
- 0x80058AF8, the end of the drop loop, which ends the capture.

- the calls of blob_weighted_center (0x80058D1F) from chamber_light_view
  (0x8005FE3E) and prim_group_draw (0x8005F157): reference point and output
  pointers at the call, both outputs at the return (0x8005FE43, 0x8005F15C;
  prim_group_draw overwrites the second one right after).

Options for the C6 follow-ups:

- --loop sets the byte anim_object+0xA (0x8006F05A) to 1 at the first stop,
  so that the animation starts again at t >= 8 and calls blob_restart; the
  targets then apply to every pass;
- --rand-poke T:VALUE writes VALUE to the blob's generator 0x800708BC at the
  first stop with t >= T, before the update;
- --max-frames N ends the run after N frames.

Every frame also records the generator state 0x800708BC.

Once, at the first capture, it reads the static meshes of blob_setup: both
vertex buffers (+0x00, +0x0C), both index buffers (+0x04, +0x18) and the
copy of the unit vectors (+0x1C).

Output: out/xemu/runs/<name>/probe_c6.json
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

BLOB_UPDATE = 0x80059789
ANIM_DRAW = 0x80056D0B
GLOW_COLOUR = 0x80058592
GLOW_QUAD = 0x800586ED
VS_CONST = 0x80040520
PS_CONST = 0x800410B0
DRAW_END = 0x80058AF8
BLOB_DRAW = (0x800583BF, 0x80058B2C)
BLOB = 0x8006FBAC
BLOB_SIZE = 0xA38
ANIM = (0x8006F050, BLOB - 0x8006F050)
T_ADDR = 0x8006F060
RAND = 0x800708BC
LOOP_FLAG = 0x8006F05A
WC_CALLS = {0x8005FE3E: 0x8005FE43, 0x8005F157: 0x8005F15C}


def u32(b, o=0):
    return struct.unpack_from('<I', b, o)[0]


def read_vb(g, res_ptr, size):
    res = g.read(res_ptr, 8)
    return g.read(0x80000000 | u32(res, 4), size).hex()


def read_static(g, blob):
    n_big, n_small = u32(blob, 0x20), u32(blob, 0x24)
    i_big, i_small = u32(blob, 0x28), u32(blob, 0x2C)
    ib = lambda p, n: g.read(u32(g.read(p, 8), 4), n * 2).hex()  # noqa: E731
    return {'counts': [n_big, n_small, i_big, i_small, u32(blob, 0x30)],
            'vb_small': read_vb(g, u32(blob, 0x00), n_small * 12),
            'ib_small': ib(u32(blob, 0x04), i_small),
            'vb_big': read_vb(g, u32(blob, 0x0C), n_big * 12),
            'ib_big': ib(u32(blob, 0x18), i_big),
            'unit': g.read(u32(blob, 0x1C), n_big * 12).hex()}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--t', action='append', type=float, default=[])
    ap.add_argument('--timeout', type=float, default=900)
    ap.add_argument('--loop', action='store_true')
    ap.add_argument('--rand-poke')
    ap.add_argument('--max-frames', type=int, default=0)
    a = ap.parse_args()
    targets = sorted(a.t)
    poke = None
    if a.rand_poke:
        pt, pv = a.rand_poke.split(':')
        poke = (float(pt), int(pv, 0))
    prev_t, wc_open = None, {}
    ns = argparse.Namespace(name=a.name, frame=[], frames=None, brk='gdb', video=False,
                            audio=False, duration=0, xemu='release', timeout=a.timeout,
                            keep_image=False, trace=[])
    b = bench.Bench(ns)
    b.start()
    frames, caps, static, cur, dup = [], [], None, None, 0
    capture_bps = (ANIM_DRAW, BLOB_DRAW[0], GLOW_COLOUR, GLOW_QUAD, DRAW_END) +         tuple(WC_CALLS) + tuple(WC_CALLS.values())
    last_key = None
    try:
        g = GDB(bench.GDB_PORT)
        g.hbreak(BLOB_UPDATE)
        armed = {BLOB_UPDATE}

        def disarm():
            for x in capture_bps + (VS_CONST, PS_CONST):
                if x in armed:
                    g.hbreak(x, False)
                    armed.discard(x)

        while True:
            if a.max_frames and len(frames) >= a.max_frames:
                b.log['notes'].append(f'stopped after {a.max_frames} frames')
                break
            if time.monotonic() - b.t0 > a.timeout:
                b.log['notes'].append('timeout')
                break
            g.cont()
            if g.wait_stop(30) is None:
                b.log['notes'].append('no hit for 30 s, run ended')
                break
            r = g.regs()
            eip, esp = r['eip'], r['esp']
            if eip == BLOB_UPDATE:
                st = g.read(esp, 12)
                key = (esp, st)
                if key == last_key:
                    dup += 1          # second stop without progress, see C3
                else:
                    last_key = key
                    if cur is not None:
                        caps.append(cur)
                        cur = None
                        disarm()
                    t, dt = struct.unpack_from('<2f', st, 4)
                    if a.loop and not frames:
                        g.write(LOOP_FLAG, b'')
                    if prev_t is not None and t < prev_t:
                        targets = sorted(a.t)      # a new pass
                    prev_t = t
                    blob = g.read(BLOB, BLOB_SIZE)
                    fr = {'args': st[4:].hex(), 'anim': g.read(T_ADDR, 0x14).hex(),
                          'blob': blob.hex(), 'rand': f'{u32(g.read(RAND, 4)):#010x}'}
                    if poke and t >= poke[0]:
                        g.write(RAND, struct.pack('<I', poke[1]))
                        fr['rand_poked'] = f'{poke[1]:#010x}'
                        poke = None
                    frames.append(fr)
                    if targets and t >= targets[0]:
                        while targets and t >= targets[0]:
                            hit = targets.pop(0)
                        if static is None:
                            static = read_static(g, blob)
                        cur = {'target': hit, 't': t, 'frame': len(frames) - 1,
                               'vs': [], 'ps': [], 'wc': [], 'done': False}
                        for x in capture_bps:
                            g.hbreak(x)
                            armed.add(x)
            elif cur is not None and eip == ANIM_DRAW and 'deformed' not in cur:
                blob = g.read(BLOB, 0x40)
                cur['deformed'] = read_vb(g, u32(blob, 0x14), u32(blob, 0x20) * 16)
                cur['anim_obj'] = g.read(*ANIM).hex()
            elif cur is not None and eip == BLOB_DRAW[0]:
                for x in (VS_CONST, PS_CONST):
                    g.hbreak(x)
                    armed.add(x)
            elif cur is not None and eip == GLOW_COLOUR:
                cur['colour'] = f"{r['eax']:#010x}"
            elif cur is not None and eip == GLOW_QUAD:
                p = u32(g.read(r['ebp'] + 0x70, 4))
                cur['quad'] = g.read(p, 0x50).hex()
            elif cur is not None and eip in (VS_CONST, PS_CONST):
                st = g.read(esp, 16)
                ret, idx, ptr, n = struct.unpack('<4I', st)
                if BLOB_DRAW[0] <= ret < BLOB_DRAW[1]:
                    cur['vs' if eip == VS_CONST else 'ps'].append(
                        {'ret': f'{ret:#010x}', 'index': idx, 'count': n,
                         'data': g.read(ptr, n * 16).hex()})
            elif cur is not None and eip in WC_CALLS:
                st = g.read(esp, 20)
                p1, p2 = struct.unpack_from('<2I', st)
                wc_open[WC_CALLS[eip]] = {'caller': f'{eip:#010x}', 'ref': st[8:20].hex(),
                                          'p1': p1, 'p2': p2}
            elif cur is not None and eip in WC_CALLS.values() and eip in wc_open:
                w = wc_open.pop(eip)
                cur['wc'].append({'caller': w['caller'], 'ref': w['ref'],
                                  'center': g.read(w['p1'], 12).hex(),
                                  'second': g.read(w['p2'], 4).hex()})
            elif cur is not None and eip == DRAW_END:
                cur['done'] = True
                caps.append(cur)
                cur = None
                disarm()
            # Continuing on a breakpoint address hits it again at once; step over it.
            if eip in armed:
                g.hbreak(eip, False)
                g.req('s')
                g.hbreak(eip)
        disarm()
        for x in list(armed):
            g.hbreak(x, False)
        g.close()
    finally:
        b.quit()
    if cur is not None:
        caps.append(cur)
    b.log['probe_c6'] = {'targets': a.t, 'frames': len(frames), 'duplicates': dup,
                         'captures': [c['t'] for c in caps]}
    with open(os.path.join(b.dir, 'probe_c6.json'), 'w') as f:
        json.dump({'static': static, 'frames': frames, 'captures': caps}, f)
    with open(os.path.join(b.dir, 'run.json'), 'w') as f:
        json.dump(b.log, f, indent=1)
    print(f'{len(frames)} frames ({dup} duplicate stops), {len(caps)} captures at '
          f't = {[round(c["t"], 4) for c in caps]} -> {b.dir}')


if __name__ == '__main__':
    main()
