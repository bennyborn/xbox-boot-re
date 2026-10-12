"""Builds the core for the host and for wasm32 and runs the host tests.

    python core/build.py          build both targets, run all tests
    python core/build.py --no-test
    python core/build.py --sweep  also measure fsincos over every float32
                                  argument up to 2 pi (about a minute)
    python core/build.py --cost   also measure the cost of the core, with
                                  the instructions of some frames counted by
                                  single stepping (some minutes), into
                                  out/core/k8/
    python core/build.py --run    also let the native host write a whole run
                                  into out/run/ (about 90 MB)

Compiler: clang from .tools/llvm/ (docs/provenance.md), or XBOOT_CLANG if set
(used by CI, where only the wasm32 target is built). Native builds use
clang's default target on this host (x86_64-pc-windows-msvc, linked against
the installed MSVC runtime); wasm32 is freestanding, without Emscripten and
without a libc. Flags follow docs/core.md, "Numerik": C99, IEEE float, no
contraction, no fast-math. Output goes to out/core/.

Steps:
  1. tools/pack/pack.py --check   the data pack, compared with its hash
  2. out/core/native/test_k*.exe  core + test, one per milestone
     out/core/native/xboot_run.exe the native host (hosts/native/)
  3. out/core/wasm/xboot.wasm     the core alone; must have no imports
  4. each test_k*.exe and node core/test/wasm_k*.mjs against the vectors;
     the digest of a run over 8 s (K2 to K4, K6, K7) or of all textures
     (K5) must be the same in both builds; for K8 the digest of the whole
     run, which the native host must give as well, and the digest of its
     draw lists, which must be that of test_k6
  5. core/test/check_k1.py        constants against the kernel, core against
                                  the Python model on synthetic tick runs
     core/test/check_k2.py        constants, geometry against model and guest,
                                  camera and chamber over the time line
     core/test/check_k3.py        the blob against the guest and the model
     core/test/check_k4.py        the logo against the guest's memory and
                                  method stream and against the model
     core/test/check_k5.py        the textures: constants and immediates,
                                  against the guest's dumps and the model
     core/test/check_k6.py        effects and frames: constants, every
                                  constant, colour and matrix of the recorded
                                  frames against the guest's command stream
     core/test/check_k7.py        the sound: constants, tables, sequencer and
                                  voice layer against the model, the voices
                                  against the render, the GP stage against the
                                  program on xemu's DSP core, the output
                                  against the recordings
  6. every call xboot.h declares is exported from the wasm module
  7. with --sweep: out/core/native/sweep_k1.exe up to 2 pi (docs/core.md 4)
"""

import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
CLANG = os.environ.get('XBOOT_CLANG', os.path.join(ROOT, '.tools', 'llvm', 'bin', 'clang.exe'))
OUT = os.path.join(ROOT, 'out', 'core')
PACK = os.path.join(ROOT, 'out', 'pack', 'xboot.pak')
VECTORS = os.path.join(ROOT, 'docs', 'vectors')

CORE = ['core/src/core.c', 'core/src/pack.c', 'core/src/Timing.c', 'core/src/x87.c',
        'core/src/Overview.c', 'core/src/Camera.c', 'core/src/Chamber.c', 'core/src/Blob.c',
        'core/src/Logo.c', 'core/src/Textures.c', 'core/src/Effects.c', 'core/src/FrameOrder.c',
        'core/src/Audio.c']
TESTS = ['test_k0', 'test_k1', 'test_k2', 'test_k3', 'test_k4', 'test_k5', 'test_k6', 'test_k7', 'test_k8']
# the whole run of K8 is shared with the native host and needs no vector
RUN = ['hosts/native/run.c']
VECTOR = {'test_k0': 'c3_timing.json', 'test_k1': 'c3_timing.json', 'test_k2': 'c5_chamber.json',
          'test_k3': 'c6_blob.json', 'test_k4': 'c7_logo.json', 'test_k5': 'c8_gpu.json',
          'test_k6': 'c8_gpu.json', 'test_k7': 'c4_audio.json', 'test_k8': None}
EXPORTS = ['xb_state_size', 'xb_init', 'xb_advance', 'xb_advance_free', 'xb_time',
           'xb_last_tick', 'xb_get_timing', 'xb_camera_at', 'xb_get_campath', 'xb_select_path', 'xb_light_view',
           'xb_chamber_part', 'xb_chamber_at', 'xb_chamber_world', 'xb_chamber_mesh_info',
           'xb_chamber_mesh', 'xb_get_blob', 'xb_blob_mesh', 'xb_blob_deformed', 'xb_blob_draw',
           'xb_blob_center', 'xb_blob_light_view', 'xb_blob_update', 'xb_blob_restart', 'xb_blob_set_rng',
           'xb_logo_mesh_info', 'xb_logo_mesh', 'xb_get_logo', 'xb_logo_draw', 'xb_logo_update',
           'xb_texture_info', 'xb_texture',
           'xb_get_track', 'xb_chamber_draw', 'xb_blob_light_camera', 'xb_fx_draw', 'xb_fx_quads',
           'xb_frame', 'xb_frame_from', 'xb_frame_setup', 'xb_audio', 'xb_audio_size', 'xb_audio_init', 'xb_audio_block',
           'xb_audio_mute', 'xb_get_audio', 'xb_audio_gp', 'xb_audio_table']

COMMON = ['-std=c99', '-O2', '-ffp-contract=off', '-fno-fast-math', '-fno-math-errno', '-Wall', '-Wextra',
          '-Wpedantic', '-Werror', '-Icore/include', '-Icore/src', '-Ihosts/native']
# MSVC's C library warns about fopen; the test is the only user.
NATIVE = COMMON + ['-D_CRT_SECURE_NO_WARNINGS']
WASM = COMMON + ['--target=wasm32', '-ffreestanding', '-nostdlib', '-fno-builtin',
                 '-Wl,--no-entry', '-Wl,--export=__heap_base', '-Wl,--strip-all'] + \
       ['-Wl,--export=' + e for e in EXPORTS] + \
       os.environ.get('XBOOT_WASM_EXTRA_FLAGS', '').split()


def show(cmd):
    print('>', ' '.join(os.path.relpath(c, ROOT) if os.path.isabs(c) and c.startswith(ROOT) else c for c in cmd),
          flush=True)


def run(cmd):
    show(cmd)
    subprocess.check_call(cmd, cwd=ROOT)


def run_out(cmd):
    """run() that also returns the output; fails like run()."""
    show(cmd)
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    sys.stdout.flush()
    if r.returncode:
        raise subprocess.CalledProcessError(r.returncode, cmd)
    return r.stdout


def wasm_imports(path):
    """Number of entries in the import section (id 2) of a wasm module."""
    data = open(path, 'rb').read()
    assert data[:8] == b'\0asm\1\0\0\0'

    def leb(i):
        val = shift = 0
        while True:
            b = data[i]
            i += 1
            val |= (b & 0x7F) << shift
            shift += 7
            if b < 0x80:
                return val, i
    i = 8
    while i < len(data):
        sec = data[i]
        size, i = leb(i + 1)
        if sec == 2:
            return leb(i)[0]
        i += size
    return 0


def main():
    test = '--no-test' not in sys.argv[1:]
    os.makedirs(os.path.join(OUT, 'native'), exist_ok=True)
    os.makedirs(os.path.join(OUT, 'wasm'), exist_ok=True)

    run([sys.executable, 'tools/pack/pack.py', '--check'])

    exes = {}
    for t in TESTS:
        exes[t] = os.path.join(OUT, 'native', t + '.exe')
        run([CLANG] + NATIVE + CORE + (RUN if t == 'test_k8' else []) +
            ['core/test/%s.c' % t, 'core/test/json.c', 'core/test/sha256.c', '-o', exes[t]])
    host = os.path.join(OUT, 'native', 'xboot_run.exe')
    run([CLANG] + NATIVE + CORE + RUN + ['hosts/native/xboot_run.c', '-o', host])

    wasm = os.path.join(OUT, 'wasm', 'xboot.wasm')
    run([CLANG] + WASM + CORE + ['-o', wasm])
    imports = wasm_imports(wasm)
    print(f'{os.path.relpath(wasm, ROOT)}: {os.path.getsize(wasm)} bytes, {imports} imports')
    if imports:
        sys.exit('the wasm core must not import anything')
    declared = set(re.findall(r'^[a-z_0-9]+ \*?(xb_[a-z_0-9]+)\(',
                              open(os.path.join(ROOT, 'core', 'include', 'xboot.h')).read(), re.M))
    if declared != set(EXPORTS):
        sys.exit('xboot.h and the exports differ: %s' % sorted(declared ^ set(EXPORTS)))
    print(f'{len(EXPORTS)} calls declared in xboot.h, all exported')

    if test:
        lists = {}
        for t in TESTS:
            vec = [os.path.join(VECTORS, VECTOR[t])] if VECTOR[t] else []
            digests = {}
            for name, cmd in (('native', [exes[t], PACK] + vec),
                              ('wasm', ['node', 'core/test/wasm_%s.mjs' % t[5:], wasm, PACK] + vec)):
                out = run_out(cmd)
                digests.update({name: line for line in out.splitlines() if line.startswith('digest ')})
                # the digest over the draw lists alone: test_k6 prints it as
                # its digest, test_k8 as "frames"
                key = {'test_k6': 'digest ', 'test_k8': 'frames '}.get(t)
                if key and name == 'native':
                    lists[t] = [line.split()[1] for line in out.splitlines() if line.startswith(key)][0]
            if not digests:
                continue
            print(f"{t}: native {digests.get('native')}, wasm {digests.get('wasm')}")
            if not digests.get('native') or digests.get('native') != digests.get('wasm'):
                sys.exit(f'{t}: the digest differs between the native and the wasm build')
            print(f'{t}: the digest is the same in both builds')
        if lists.get('test_k6') != lists.get('test_k8'):
            sys.exit(f"the draw lists of the whole run differ from test_k6's: {lists}")
        print(f"test_k8: the draw lists of the whole run have the digest of test_k6, {lists['test_k8']}")
        out = run_out([host, PACK, '--digest'])
        if out.strip() != digests.get('native'):
            sys.exit('the native host gives another digest than test_k8')
        print('xboot_run: the digest of the native host is that of test_k8')
        run([sys.executable, 'core/test/check_k1.py', exes['test_k1']])
        run([sys.executable, 'core/test/check_k2.py', exes['test_k2']])
        run([sys.executable, 'core/test/check_k3.py', exes['test_k3']])
        run([sys.executable, 'core/test/check_k4.py', exes['test_k4']])
        run([sys.executable, 'core/test/check_k5.py', exes['test_k5']])
        run([sys.executable, 'core/test/check_k6.py', exes['test_k6']])
        run([sys.executable, 'core/test/check_k7.py', exes['test_k7']])

    if '--run' in sys.argv[1:]:
        os.makedirs(os.path.join(ROOT, 'out', 'run', 'assets'), exist_ok=True)
        run([host, PACK, os.path.join(ROOT, 'out', 'run')])

    if '--cost' in sys.argv[1:]:
        k8 = os.path.join(OUT, 'k8')
        os.makedirs(k8, exist_ok=True)
        cost = os.path.join(OUT, 'native', 'cost_k8.exe')
        run([CLANG] + NATIVE + CORE + ['core/test/cost_k8.c', '-o', cost])
        run([cost, PACK, '--json', os.path.join(k8, 'cost.json')])
        run(['node', 'core/test/cost_k8.mjs', wasm, PACK])

    if '--sweep' in sys.argv[1:]:
        sweep = os.path.join(OUT, 'native', 'sweep_k1.exe')
        run([CLANG] + NATIVE + ['core/src/x87.c', 'core/test/sweep_k1.c', '-o', sweep])
        run([sweep, '0x40C90FE0'])


if __name__ == '__main__':
    main()
