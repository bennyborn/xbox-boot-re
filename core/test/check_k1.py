"""K1 checks that need Python: constants and the executable specification.

    python core/test/check_k1.py out/core/native/test_k1.exe

1. Every constant `#define K_* <value>f /* <address> */` in core/src/Timing.c
   is compared with the float32 at that address in out/rom/kernel.img
   (file offset = address - 0x80010000, Overview.md). The constants of
   core/src/x87.c are checked by core/test/gen_trig.py, run from here.
2. The core runs synthetic tick sequences through `test_k1 --dump` and is
   compared bit for bit with the model in tools/xemu/vectors_c3.py, which
   Timing.md calls its executable form. The sequences reach parts of the
   time line the two measured runs do not: every millisecond from 0 to 8 s,
   a steady 60 Hz, random frame lengths, and steps above 1 s and backwards.
   The measured runs themselves are checked in test_k1.c.
"""

import os
import random
import re
import struct
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'xemu'))
import vectors_c3 as model  # noqa: E402

KERNEL = os.path.join(ROOT, 'out', 'rom', 'kernel.img')
BASE = 0x80010000
TMP = os.path.join(ROOT, 'out', 'core', 'k1')

checks = failed = 0


def check(ok, what):
    global checks, failed
    checks += 1
    failed += not ok
    print(('ok    ' if ok else 'FAIL  ') + what)


def f32bits(x):
    return struct.unpack('<I', struct.pack('<f', float(x)))[0]


def constants():
    src = open(os.path.join(ROOT, 'core', 'src', 'Timing.c'), encoding='utf-8').read()
    img = open(KERNEL, 'rb').read()
    found = re.findall(r'#define\s+(K_\w+)\s+([0-9.]+)f\s+/\*\s*(0x[0-9A-Fa-f]{8})\s*\*/', src)
    bad = []
    for name, value, addr in found:
        a = int(addr, 16)
        rom = struct.unpack_from('<f', img, a - BASE)[0]
        if f32bits(rom) != f32bits(float(value)):
            bad.append(f'{name} {value} at {addr}: kernel {rom!r}')
    for b in bad:
        print('      ' + b)
    check(len(found) >= 16 and not bad,
          f'{len(found)} constants of Timing.c equal the kernel image, {len(bad)} differ')
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'core', 'test', 'gen_trig.py')],
                       capture_output=True, text=True)
    print('      ' + r.stdout.strip().replace('\n', '\n      '))
    check(r.returncode == 0, 'constants of x87.c equal their derivation (gen_trig.py)')


def model_run(short, start, ticks):
    table, _ = model.pulse_table()
    st = {'t': model.f32(7.5 if short else 0.0), 'x18': model.f32(0), 'x24': model.f32(0),
          'x1c': model.f32(0), 'x20': model.f32(0)}
    out, last = [], start
    for tick in ticks:
        if st['t'] >= 8.0:
            out.append((1, st))
            continue
        d = abs(tick - last)            # ticks stay far from 2^31 here
        last = tick
        st = model.step(st, table, d)
        out.append((1 if st['t'] >= 8.0 else 0, st))
    return [f'{rc} ' + ' '.join(f'{f32bits(s[k]):08X}' for k in ('t', 'x18', 'x1c', 'x20', 'x24'))
            for rc, s in out]


def sequence(name, short, start, steps):
    ticks, t = [], start
    for d in steps:
        t += d
        ticks.append(t)
    return name, short, start, ticks


def sequences():
    rnd = random.Random(1)
    yield sequence('every ms, long', False, 398, [1] * 8002)
    yield sequence('every ms, short', True, 420, [1] * 502)
    yield sequence('60 Hz (17/16/17 ms)', False, 1000, [17, 16, 17] * 170)
    yield sequence('random 0..40 ms', False, 5, [rnd.randint(0, 40) for _ in range(800)])
    yield sequence('random 0..300 ms, short', True, 7, [rnd.randint(0, 300) for _ in range(40)])
    # Above 1 s and backwards; tick stays positive.
    odd = []
    for i in range(400):
        r = rnd.random()
        odd.append(rnd.randint(1001, 5000) if r < 0.05 else -rnd.randint(1, 30) if r < 0.15
                   else rnd.randint(1, 40))
    yield sequence('jumps above 1 s and backwards', False, 100000, odd)


def against_model(exe):
    os.makedirs(TMP, exist_ok=True)
    total = 0
    for name, short, start, ticks in sequences():
        path = os.path.join(TMP, 'ticks.txt')
        with open(path, 'w') as f:
            f.write(f'{1 if short else 0} {start}\n' + '\n'.join(map(str, ticks)) + '\n')
        got = subprocess.run([exe, os.path.join(ROOT, 'out', 'pack', 'xboot.pak'), '--dump', path],
                             capture_output=True, text=True, check=True).stdout.split('\n')[:len(ticks)]
        want = model_run(short, start, ticks)
        diff = [i for i, (g, w) in enumerate(zip(got, want)) if g != w]
        steps = len(want)
        total += steps
        for i in diff[:3]:
            print(f'      step {i}: core {got[i]}, model {want[i]}')
        check(not diff and len(got) == len(want),
              f'{name}: {steps} steps, last t {struct.unpack(">f", bytes.fromhex(want[-1].split()[1]))[0]:.6f},'
              f' {len(diff)} differ from the model')
    print(f'      {total} steps against the model')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.splitlines()[2].strip())
    constants()
    against_model(sys.argv[1])
    print(f'{checks} checks, {failed} failed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
