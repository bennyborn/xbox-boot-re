"""Records the logo object in every frame and the GPU method stream of the
logo phase.

    python tools/xemu/probe_c7.py --name c7_main --trace-from 5.0

Starts xemu like probe.py and stops at the entry of anim_draw (0x80056D0B)
in every frame. Each stop reads t, x1c, x20 of the animation object
(0x8006F060-0x8006F073), its switch byte +0xE (0x8006F05E), the whole logo
object (0x8006F5CC, 0x110 bytes), the logo's frame matrix this+0x1774
(0x800707C4) and the camera's view and projection (0x8006F834, 0x8006F874),
together with the position reached in trace.log.

From the first stop with t >= --trace-from on, the trace events
nv2a_pgraph_method* are switched on over QMP and stay on to the end.

A second breakpoint at 0x8005C43F, the call of logo_draw inside
logo_render_to_texture, records t and the trace position whenever the logo
is drawn into its texture.

Once, at the first stop at which the logo object holds its buffers, the
script reads every vertex buffer (resource word 1 is the physical address),
every decoded index list, the texture of the two quads (+0xC0) and the
screen quad of the veil object (*0x8007083C, 4 x 0x1C bytes).

Output: out/xemu/runs/<name>/probe_c7.json
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
RTT_CALL = 0x8005C43F
LOGO = 0x8006F5CC
LOGO_SIZE = 0x110
T_ADDR = 0x8006F060
SWITCH = 0x8006F05E
FRAME_MATRIX = 0x800707C4
VIEW, PROJ = 0x8006F834, 0x8006F874
VEIL_QUAD = 0x8007083C

# Slots of the logo object: vertex buffer, index list, vertex count, index
# count, vertex size (logo_build_meshes 0x8005B712, logo_build_mesh_e
# 0x8005B934, logo_build_quads 0x8005B9A8).
SLOTS = {'A': 0x00, 'B': 0x14, 'C': 0x28, 'D': 0x3C, 'E': 0x50, 'Q1': 0xA0, 'Q2': 0xB0}
VSIZE = {'E': 12}


def u32(b, o=0):
    return struct.unpack_from('<I', b, o)[0]


def read_static(g, logo):
    out = {}
    for name, s in SLOTS.items():
        res, idx, nv, ni = struct.unpack_from('<4I', logo, s)
        phys = u32(g.read(res, 8), 4)
        out[name] = {'res': f'{res:#010x}', 'phys': f'{phys:#010x}', 'nv': nv, 'ni': ni,
                     'vb': g.read(0x80000000 | phys, nv * VSIZE.get(name, 20)).hex(),
                     'ib': g.read(idx, ni * 2).hex()}
    tex = u32(logo, 0xC0)
    tphys = u32(g.read(tex, 8), 4)
    out['quad_texture'] = {'res': f'{tex:#010x}', 'phys': f'{tphys:#010x}',
                           'head': g.read(tex, 0x14).hex(),
                           'data': g.read(0x80000000 | tphys, 0x400).hex()}
    vq = u32(g.read(VEIL_QUAD, 4))
    vphys = u32(g.read(vq, 8), 4)
    out['veil_quad'] = {'res': f'{vq:#010x}', 'phys': f'{vphys:#010x}',
                        'vb': g.read(0x80000000 | vphys, 4 * 0x1C).hex()}
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--trace-from', type=float, default=5.0)
    ap.add_argument('--timeout', type=float, default=900)
    a = ap.parse_args()
    ns = argparse.Namespace(name=a.name, frame=[], frames=None, brk='gdb', video=False,
                            audio=False, duration=0, xemu='release', timeout=a.timeout,
                            keep_image=False, trace=[])
    b = bench.Bench(ns)
    b.start()
    frames, rtt, static, dup, tracing, last_key = [], [], None, 0, None, None
    bps = (ANIM_DRAW, RTT_CALL)
    try:
        g = GDB(bench.GDB_PORT)
        for x in bps:
            g.hbreak(x)
        while time.monotonic() - b.t0 < a.timeout:
            g.cont()
            if g.wait_stop(30) is None:
                b.log['notes'].append('no hit for 30 s, run ended')
                break
            r = g.regs()
            b.tr.update()
            eip, esp = r['eip'], r['esp']
            anim = g.read(T_ADDR, 0x14)
            t = struct.unpack_from('<f', anim)[0]
            if eip == ANIM_DRAW:
                key = (esp, anim)
                if key == last_key:
                    dup += 1          # second stop without progress, see C3
                else:
                    last_key = key
                    logo = g.read(LOGO, LOGO_SIZE)
                    if static is None and u32(logo, 0x3C) and u32(logo, 0x50):
                        static = read_static(g, logo)
                    if tracing is None and t >= a.trace_from:
                        b.q.cmd('trace-event-set-state', name=bench.TRACE_METHODS, enable=True)
                        tracing = b.tr.pos
                    frames.append({'trace_pos': b.tr.pos, 'flips': b.tr.flips,
                                   'anim': anim.hex(), 'switch': g.read(SWITCH, 1).hex(),
                                   'logo': logo.hex(),
                                   'frame_matrix': g.read(FRAME_MATRIX, 0x40).hex(),
                                   'view': g.read(VIEW, 0x40).hex(),
                                   'proj': g.read(PROJ, 0x40).hex()})
            elif eip == RTT_CALL:
                rtt.append({'trace_pos': b.tr.pos, 'flips': b.tr.flips, 'anim': anim.hex(),
                            'frame': len(frames) - 1,
                            'logo': g.read(LOGO, LOGO_SIZE).hex(),
                            'view': g.read(VIEW, 0x40).hex(),
                            'proj': g.read(PROJ, 0x40).hex()})
            # Continuing on a breakpoint address hits it again at once; step over it.
            if eip in bps:
                g.hbreak(eip, False)
                g.req('s')
                g.hbreak(eip)
        for x in bps:
            g.hbreak(x, False)
        g.close()
    finally:
        b.quit()
    b.tr.update()
    b.log['probe_c7'] = {'trace_from': a.trace_from, 'trace_on_at': tracing,
                         'frames': len(frames), 'duplicates': dup, 'rtt': len(rtt)}
    with open(os.path.join(b.dir, 'probe_c7.json'), 'w') as f:
        json.dump({'trace_on_at': tracing, 'trace_end': b.tr.pos, 'static': static,
                   'frames': frames, 'rtt': rtt}, f)
    with open(os.path.join(b.dir, 'run.json'), 'w') as f:
        json.dump(b.log, f, indent=1)
    print(f'{len(frames)} frames ({dup} duplicate stops), {len(rtt)} texture draws, '
          f'trace on at {tracing} -> {b.dir}')


if __name__ == '__main__':
    main()
