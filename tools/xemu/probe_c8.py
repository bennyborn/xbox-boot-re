"""Records every call from the animation into the graphics code, with the
pushbuffer words each call leaves, for setup and chosen frames (C8).

    python tools/xemu/probe_c8.py --name c8_main

Breakpoints (GDB stub, as in probe.py):

- 0x800579F4 anim_main and 0x80057949 anim_frame, always: the frame
  boundary; t of the animation object (0x8006F060) and the trace position.
- 0x80056D0B anim_draw, while armed: the animation object after the
  update (+0x00..+0x30), the camera block +0x690..+0x7A0 and the fx object
  +0x17EC..+0x1814, for the frame being drawn.
- every call instruction in 0x80055E75-0x800626DF whose target lies in the
  graphics code 0x8003CB40-0x80044F40 (out/ghidra/kernel_graph.json, 510
  sites), only while armed. In the setup the sites of prim_group_draw
  (0x8005EE08) other than its three draw calls stay off. Armed are the setup (anim_main to the first
  anim_frame) and every frame chosen by --at (first frame whose previous t
  reaches the value) or lying in a --window.

At each call-site stop the script reads the registers, eight stack words,
the return addresses at [ebp+4] and [[ebp]+4] and
the pushbuffer state of the graphics device at 0x8004AA80 (put, limit,
start, end: +0x00, +0x04, +0x10, +0x14; device pointer at 0x8004D610).
The words between the put pointer of the previous stop and this one belong
to the previous call; a wrap is followed through the jump word to the ring
start (0x80042710). Words are stored raw; tools/xemu/nv2a.py decodes them.

For every texture handed to 0x8003EFD0 (stage, resource) the resource
(0x14 bytes: word 1 physical address, word 3 format) and its data are read
once (length from tools/xemu/nv2a.py texture_length).

The trace events nv2a_pgraph_method* are on from the first armed frame and
stay on --trace-tail frames after the last.

Output: out/xemu/runs/<name>/probe_c8.json
"""

import argparse
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
import nv2a  # noqa: E402
from gdbstub import GDB  # noqa: E402

ROOT = bench.ROOT
ANIM_MAIN, ANIM_FRAME = 0x800579F4, 0x80057949
ANIM_DRAW = 0x80056D0B
ANIM_OBJ = 0x8006F050
T_ADDR = 0x8006F060
DEVICE = 0x8004AA80
SET_TEXTURE = 0x8003EFD0
LO, HI = 0x80055E75, 0x800626DF
LIB_LO, LIB_HI = 0x8003CB40, 0x80044F40
PRIM_GROUP_DRAW = 0x8005EE08
DRAW_FUNCS = (0x8003F620, 0x8003F6C0)
# Texture and surface constructors with the stack index of their out pointer
# (0x8003E630 -> 0x800406B0, 0x8003E660 cube, 0x8003E690 / 0x8003E6B0 ->
# 0x800413F0).
CREATE_OUT = {0x8003E630: 6, 0x8003E660: 5, 0x8003E690: 5, 0x8003E6B0: 4}


def u32(b, o=0):
    return struct.unpack_from('<I', b, o)[0]


def call_sites():
    funcs = json.load(open(os.path.join(ROOT, 'out', 'ghidra', 'kernel_graph.json')))
    sites, owner = {}, {}
    for f in funcs:
        if LO <= int(f['entry'], 16) <= HI:
            for s, t in f['calls']:
                if t != 'indirect' and LIB_LO <= int(t, 16) < LIB_HI:
                    sites[int(s, 16)] = int(t, 16)
                    owner[int(s, 16)] = int(f['entry'], 16)
    return sites, owner


def read_frame(g, ebp):
    """Saved ebp and return address of a stack frame, zeros if ebp is no
    frame pointer."""
    if not 0x80000000 <= ebp < 0xF0000000:
        return bytes(8)
    try:
        return g.read(ebp, 8)
    except RuntimeError:
        return bytes(8)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--at', type=float, action='append', default=[])
    ap.add_argument('--window', action='append', default=[], help='A:B in t')
    ap.add_argument('--trace-tail', type=int, default=3)
    ap.add_argument('--no-setup', action='store_true')
    ap.add_argument('--timeout', type=float, default=2400)
    a = ap.parse_args()
    if not a.at and not a.window:
        a.at = [0.0, 0.3, 1.0, 2.0, 3.0, 4.0, 4.6, 5.0, 5.2, 5.4, 5.7, 7.0, 7.9]
        a.window = ['5.88:6.45']
    windows = [tuple(map(float, w.split(':'))) for w in a.window]
    ats = sorted(a.at)
    sites, owner = call_sites()
    # In the setup, prim_group_draw runs once per part and cube face
    # (texgen_chamber_cube); stopping there 13 000 times delays the setup by
    # minutes, after which the first frame never came (run c8_smoke). Its
    # sites stay off in the setup; the frames record them.
    setup_skip = {s for s in sites if owner[s] == PRIM_GROUP_DRAW
                  and sites[s] not in DRAW_FUNCS}

    ns = argparse.Namespace(name=a.name, frame=[], frames=None, brk='gdb', video=False,
                            audio=False, duration=0, xemu='release', timeout=a.timeout,
                            keep_image=False, trace=[])
    b = bench.Bench(ns)
    b.start()
    frames, calls, textures = [], [], {}
    armed, tracing, tail = False, False, 0
    last_put, last_ring = None, None
    pending_at = list(ats)
    stops = 0
    t_start = time.monotonic()

    active = set()

    def arm(g, on, skip=()):
        want_set = {s for s in sites if s not in skip} if on else set()
        for s in active - want_set:
            g.hbreak(s, False)
        for s in want_set - active:
            g.hbreak(s, True)
        active.clear()
        active.update(want_set)

    def dump_resource(g, res, why, call):
        head = g.read(res, 0x14)
        fmt = u32(head, 12)
        try:
            n = nv2a.texture_length(fmt)
        except KeyError:
            n = 0
        phys = u32(head, 4)
        textures[res] = {'res': f'{res:#x}', 'head': head.hex(), 'why': why,
                         'call': call, 'length': n,
                         'data': g.read(0x80000000 | phys, n).hex() if 0 < n <= 0x400000 else None}

    def read_created(g, c):
        """The resource a constructor has just written to its out pointer."""
        k = CREATE_OUT.get(int(c['target'], 16))
        if k is not None:
            out = int(c['args'][k], 16)
            if out >= 0x80000000:
                c['res'] = f'{u32(g.read(out, 4)):#x}'

    def read_segment(g, p0, ring, p1):
        """Words from p0 to p1; ring = (start, end) of the pushbuffer."""
        if p0 is None or p1 is None:
            return None, 'none'
        if p1 >= p0:
            n = p1 - p0
            if n > 0x40000:
                return None, f'too long {n:#x}'
            return g.read(p0, n).hex() if n else '', 'ok'
        start, end = ring
        if not (start <= p1 < end) or not (start <= p0 <= end):
            return None, f'outside ring {p0:#x} {p1:#x}'
        # Wrap: the words up to the jump, then from the ring start.
        head = g.read(p0, min(end - p0, 0x10000))
        words = struct.unpack(f'<{len(head) // 4}I', head)
        k = 0
        out = []
        while k < len(words):
            w = words[k]
            if (w & 3) == 1 or (w & 0xE0000003) == 0x20000000:
                out.append(w)
                break
            if (w & 0xE0030003) in (0, 0x40000000):
                c = (w >> 18) & 0x7FF
                out.extend(words[k:k + 1 + c])
                k += 1 + c
                continue
            out.append(w)
            k += 1
        else:
            return None, 'wrap without jump'
        tail_words = g.read(start, p1 - start) if p1 > start else b''
        return (struct.pack(f'<{len(out)}I', *out) + tail_words).hex(), 'wrap'

    try:
        g = GDB(bench.GDB_PORT)
        g.hbreak(ANIM_MAIN)
        g.hbreak(ANIM_FRAME)
        while time.monotonic() - b.t0 < a.timeout:
            g.cont()
            if g.wait_stop(60) is None:
                b.log['notes'].append('no hit for 60 s, run ended')
                break
            stops += 1
            r = g.regs()
            b.tr.update()
            eip, esp = r['eip'], r['esp']
            if eip == ANIM_DRAW:
                # t and the state of the animation object after the update,
                # for the frame being drawn: header +0x00..+0x30, camera
                # +0x690..+0x7A0, fx object +0x17EC..+0x1814.
                frames[-1]['draw'] = {'obj': g.read(ANIM_OBJ, 0x30).hex(),
                                      'cam': g.read(ANIM_OBJ + 0x690, 0x110).hex(),
                                      'fx': g.read(ANIM_OBJ + 0x17EC, 0x28).hex()}
            elif eip in (ANIM_MAIN, ANIM_FRAME):
                t = struct.unpack('<f', g.read(T_ADDR, 4))[0]
                if eip == ANIM_MAIN:
                    want = not a.no_setup
                else:
                    want = any(lo <= t <= hi for lo, hi in windows)
                    while pending_at and t >= pending_at[0]:
                        pending_at.pop(0)
                        want = True
                dev = g.read(DEVICE, 0x18)
                put = u32(dev, 0)
                # Close the segment of the last call before the frame boundary.
                if armed and calls and calls[-1].get('words') is None:
                    calls[-1]['words'], calls[-1]['seg'] = read_segment(
                        g, last_put, last_ring, put)
                    read_created(g, calls[-1])
                if frames and frames[-1]['eip'] == f'{ANIM_MAIN:#x}':
                    # End of the setup: every texture made there, with its data.
                    for ci, c in enumerate(calls):
                        res = int(c.get('res', '0'), 16)
                        if res >= 0x80000000 and res not in textures:
                            dump_resource(g, res, 'created', ci)
                frames.append({'eip': f'{eip:#x}', 't_prev': t, 'armed': want,
                               'trace_pos': b.tr.pos, 'flips': b.tr.flips,
                               'put': f'{put:#x}', 'ncalls': len(calls)})
                if want and not tracing:
                    b.methods(True)
                    tracing = True
                if want:
                    tail = a.trace_tail
                elif tracing:
                    tail -= 1
                    if tail <= 0:
                        b.methods(False)
                        tracing = False
                arm(g, want, setup_skip if eip == ANIM_MAIN else ())
                if want != armed:
                    g.hbreak(ANIM_DRAW, want)
                armed = want
                last_put = put
                last_ring = (u32(dev, 0x10), u32(dev, 0x14))
                print(f'frame stop {len(frames)} t={t:.3f} armed={want} calls={len(calls)} '
                      f'stops={stops} {time.monotonic() - t_start:.0f}s', flush=True)
            elif eip in sites:
                dev = g.read(DEVICE, 0x18)
                put = u32(dev, 0)
                ring = (u32(dev, 0x10), u32(dev, 0x14))
                if calls and calls[-1].get('words') is None:
                    calls[-1]['words'], calls[-1]['seg'] = read_segment(
                        g, last_put, last_ring, put)
                    read_created(g, calls[-1])
                stack = g.read(esp, 0x20)
                # Return addresses of the calling function and of its caller
                # (every function of the range keeps an ebp frame).
                # A few functions use ebp as a general register; then the
                # chain is not read.
                ebp = r['ebp']
                f1 = read_frame(g, ebp)
                f2 = read_frame(g, u32(f1, 0))
                c = {'frame': len(frames) - 1, 'site': f'{eip:#x}',
                     'ret1': f'{u32(f1, 4):#x}', 'ret2': f'{u32(f2, 4):#x}',
                     'target': f'{sites[eip]:#x}', 'put': f'{put:#x}',
                     'ecx': f'{r["ecx"]:#x}', 'edx': f'{r["edx"]:#x}',
                     'args': [f'{x:#x}' for x in struct.unpack('<8I', stack)],
                     'words': None}
                calls.append(c)
                last_put, last_ring = put, ring
                if sites[eip] == SET_TEXTURE:
                    res = u32(stack, 4)
                    if res and res not in textures:
                        dump_resource(g, res, 'bound', len(calls) - 1)
            if eip in (ANIM_MAIN, ANIM_FRAME, ANIM_DRAW) or eip in sites:
                g.hbreak(eip, False)
                g.req('s')
                if eip in (ANIM_MAIN, ANIM_FRAME) or eip in active or (eip == ANIM_DRAW and armed):
                    g.hbreak(eip)
        try:
            arm(g, False)
        except Exception:
            pass
        g.close()
    finally:
        b.quit()
    b.tr.update()
    b.log['probe_c8'] = {'setup_skip': sorted(f'{x:#x}' for x in setup_skip), 'at': ats, 'window': a.window, 'frames': len(frames),
                         'calls': len(calls), 'textures': len(textures), 'stops': stops}
    with open(os.path.join(b.dir, 'probe_c8.json'), 'w') as f:
        json.dump({'sites': {f'{k:#x}': f'{v:#x}' for k, v in sites.items()},
                   'frames': frames, 'calls': calls,
                   'textures': list(textures.values()), 'trace_end': b.tr.pos}, f)
    with open(os.path.join(b.dir, 'run.json'), 'w') as f:
        json.dump(b.log, f, indent=1)
    print(f'{len(frames)} frames, {len(calls)} calls, {len(textures)} textures -> {b.dir}')


if __name__ == '__main__':
    main()
