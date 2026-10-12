"""One xemu run up to a chosen time of the animation, with every tick.

    python tools/xemu/frame_at.py --name w1_t2000 --t 2.0

The run stops at every call of the frame function 0x80057949 (anim_frame,
docs/decomp/Timing.md 2) over the GDB stub and reads the first 0x30 bytes
of the animation object 0x8006F050: t at +0x10, the last tick at +0x28, the
tick of the set-up at +0x2C. At the first stop whose t has reached --t the
script writes a jump to itself (EB FE) over the first two bytes of the
frame function and lets the guest run: it stays where it is, the GPU
thread gets --settle seconds to finish what the guest has sent, and a
snapshot is taken from the running machine. Only then does xemu write the
drawn surfaces back into guest RAM (tools/xemu/README.md); a snapshot taken
while stopped holds neither picture nor depth. The run ends there.

At stop k the frames 0 ... k-1 have been made. The tick frame k-1 was made
with is the last tick read at stop k. So the snapshot shows the frame of
the last tick in the list, and a port given the same ticks must show the
same frame, the blob included (docs/web.md 7).

Output in out/xemu/runs/<name>/:

    capture.json   ticks relative to the set-up, t after each as bit
                   pattern, where the run stopped
    screen.png     the drawing surface 1280 x 480 at 0x3628000
    depth.bin      its depth surface at 0x33D0000: 1280 x 480 words, little
                   endian, depth in bits 8-31 (docs/decomp/GPU.md 7,
                   FrameOrder.md 1)
    display.png    the surface that is scanned out
    run.json ...   as bench.py writes them
"""

import argparse
import json
import os
import struct
import sys
import time
import types

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
from gdbstub import GDB  # noqa: E402

ANIM_FRAME = 0x80057949
ANIM_OBJ = 0x8006F050
SCREEN, DEPTH = 0x3628000, 0x33D0000
WIDTH, HEIGHT = 1280, 480


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--name', required=True)
    ap.add_argument('--t', type=float, required=True, help='stop at the first frame with t >= this')
    ap.add_argument('--settle', type=float, default=0.5)
    ap.add_argument('--timeout', type=float, default=20, help='seconds to wait for one frame')
    a = ap.parse_args()

    ns = types.SimpleNamespace(name=a.name, xemu='release', audio=False, video=False, brk='anim_frame',
                               peek=[(ANIM_OBJ, 0x30)], trace=[], patch=[], keep_image=False, dsp=False,
                               t=a.t, settle=a.settle)
    b = bench.Bench(ns)
    b.start()
    stops = []
    try:
        g = GDB(bench.GDB_PORT)
        g.hbreak(ANIM_FRAME)
        while True:
            g.cont()
            if g.wait_stop(a.timeout) is None:
                raise RuntimeError(f'no frame within {a.timeout} s after {len(stops)} stops')
            obj = g.read(ANIM_OBJ, 0x30)
            if stops and stops[-1] == obj:
                # the breakpoint held twice at the same call (FrameOrder.md 6)
                b.log['notes'].append(f'repeated stop after {len(stops)} stops')
            else:
                stops.append(obj)
                if len(stops) > 1 and struct.unpack_from('<f', obj, 0x10)[0] >= a.t:
                    break
            # Continuing on a breakpoint hits it again at once (QEMU checks
            # breakpoints before the instruction): step over it first.
            g.hbreak(ANIM_FRAME, False)
            g.req('s')
            g.hbreak(ANIM_FRAME)
        # A snapshot taken while stopped does not hold the surfaces: xemu
        # writes them back only when a running machine is saved. So the guest
        # runs, but in place: a jump to itself where it stands.
        code = g.read(ANIM_FRAME, 2)
        g.write(ANIM_FRAME, b'\xEB\xFE')
        g.hbreak(ANIM_FRAME, False)
        g.cont()
        time.sleep(a.settle)
        b.savevm('at', 'break')
        b.log['spin'] = {'at': f'{ANIM_FRAME:#010x}', 'old': code.hex(), 'new': 'ebfe'}
        g.close()
    finally:
        b.quit()
    b.finish()

    setup = struct.unpack_from('<I', stops[0], 0x2C)[0]
    cap = {'target_t': a.t, 'setup_tick': setup, 'frames': len(stops) - 1,
           'ticks': [(struct.unpack_from('<I', s, 0x28)[0] - setup) & 0xFFFFFFFF for s in stops[1:]],
           't_bits': ['%08X' % struct.unpack_from('<I', s, 0x10)[0] for s in stops[1:]],
           't': struct.unpack_from('<f', stops[-1], 0x10)[0],
           'object_at_stop': stops[-1].hex(), 'settle_s': a.settle,
           'source': 'tools/xemu/frame_at.py', 'run': a.name}
    out = os.path.join(b.dir, 'at')
    ram = np.fromfile(os.path.join(out, 'ram.bin'), np.uint8)
    cap['object_in_snapshot'] = bytes(ram[ANIM_OBJ & 0x7FFFFFFF:(ANIM_OBJ & 0x7FFFFFFF) + 0x30]).hex()
    colour = ram[SCREEN:SCREEN + WIDTH * HEIGHT * 4].reshape(HEIGHT, WIDTH, 4)[:, :, 2::-1]
    Image.fromarray(colour.copy()).save(os.path.join(b.dir, 'screen.png'))
    ram[DEPTH:DEPTH + WIDTH * HEIGHT * 4].tofile(os.path.join(b.dir, 'depth.bin'))
    if os.path.exists(os.path.join(out, 'frame.png')):
        os.replace(os.path.join(out, 'frame.png'), os.path.join(b.dir, 'display.png'))
    cap['flips'] = b.log.get('flips_total')
    for big in ('ram.bin', 'pramin.bin', 'state.json'):
        os.remove(os.path.join(out, big))
    with open(os.path.join(b.dir, 'capture.json'), 'w') as f:
        json.dump(cap, f, indent=1)
    print('%s: %d frames to t = %.6f, last tick %d, %s flips, object in the snapshot %s'
          % (a.name, cap['frames'], cap['t'], cap['ticks'][-1], cap['flips'],
             'as at the stop' if cap['object_in_snapshot'] == cap['object_at_stop'] else 'CHANGED'))


if __name__ == '__main__':
    main()
