"""Measurement bench: one xemu run of the BIOS, recorded.

    python tools/xemu/bench.py --frame 150 --video --audio
    python tools/xemu/bench.py --frame 90 --frame 150 --frames 30:450:30
    python tools/xemu/bench.py --break 0x9024C
    python tools/xemu/bench.py --frames 10:520:10 --peek 0x8006F050:0x30

"Frame N" is the machine state after the N-th NV097_FLIP_INCREMENT_WRITE
executed by PGRAPH (trace event nv2a_pgraph_flip_increment_write). The
bench follows these lines in the live trace and takes a savevm snapshot
when the count reaches N. Which flip the snapshot really follows is
decided afterwards from the write3d field of NV_PGRAPH_SURFACE in the
saved NV2A state, which every flip advances modulo 2.

Output in out/xemu/runs/<name>/:

    run.json, flips.json, trace.log, stderr.txt, xemu.toml
    video.mkv            --video: the xemu window, ffmpeg gdigrab, 60 fps
    audio.wav            --audio: host loopback input "Stereomix", 48 kHz
    frame_NNNN/          --frame N, everything; NNNN is the flip the
                         snapshot really follows, often N+1:
        ram.bin          guest RAM, 64 MiB (RAM block xbox.ram)
        pramin.bin       NV2A RAMIN, 1 MiB
        cpu.json         CPU state from the snapshot
        state.json       state of every device from the snapshot
        frame.png        the surface at PCRTC start, as scanned out
        methods.txt      PGRAPH methods from flip N-1 to flip N
    sequence/NNNN.png    --frames A:B:S, the scanned-out surface only,
                         NNNN again the flip really captured
    break_AAAAAAAA/      --break ADDR, like frame_NNNN without methods

Why savevm: xemu renders into GL textures and writes dirty surfaces back
to guest RAM only when the VM enters the save-VM state (xemu
hw/xbox/nv2a/nv2a.c, nv2a_vm_state_change). The snapshot goes to a
private qcow2 image attached with -blockdev, which the guest does not
see. On xemu 0.8.136 a savevm taken while paused works, but a following
cont, loadvm or large pmemsave ends the process; taken while running it
resumes cleanly. A breakpoint capture therefore ends the run.
"""

import argparse
import ctypes
import ctypes.wintypes as W
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qmp import QMP  # noqa: E402
from gdbstub import GDB  # noqa: E402
import qcow2  # noqa: E402
import vmstate  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
XEMU = {'release': os.path.join(ROOT, '.tools', 'xemu', 'xemu.exe'),
        'dbg': os.path.join(ROOT, '.tools', 'xemu-dbg', 'xemu.exe')}
MCPX = os.path.join(ROOT, 'rom', 'mcpx_1.0.bin')
FLASH = os.path.join(ROOT, 'rom', 'bios_complex_4627_retail.bin')
EEPROM = os.path.join(ROOT, 'out', 'xemu', 'eeprom.bin')
RUNS = os.path.join(ROOT, 'out', 'xemu', 'runs')

QMP_PORT, GDB_PORT = 4444, 1234
KERNEL_ENTRY = 0x8005504C       # PE AddressOfEntryPoint, docs/decomp/Overview.md
RAM_SIZE = 64 << 20

TRACE_ALWAYS = ['nv2a_pgraph_flip_increment_write',
                'nv2a_pgraph_surface_create_color',
                'nv2a_pgraph_surface_create_zeta',
                'nv2a_irq']
TRACE_METHODS = 'nv2a_pgraph_method*'
DOWNLOAD = 'nv2a_pgraph_surface_download'
FLIP = b'nv2a_pgraph_flip_increment_write'
IRQ = b'nv2a_irq '
NV_PMC_INTR_0_PCRTC = 1 << 24   # xemu hw/xbox/nv2a/nv2a_regs.h
FLIP_RE = re.compile(rb'nv2a_pgraph_flip_increment_write 0x([0-9a-f]+) -> 0x([0-9a-f]+)')
CREATE_RE = re.compile(
    rb'nv2a_pgraph_surface_create_color Create: \[COLOR @ 0x([0-9a-f]+) '
    rb'\((\d+)x(\d+)\)\] \((\w+)\).*?p=(\d+)')

# xemu hw/xbox/nv2a/nv2a_regs.h
NV_PGRAPH_SURFACE = 0x710
SURFACE_WRITE_3D = (20, 7)      # shift, mask
SURFACE_MODULO_3D = (28, 7)
COLOR_LE_A8R8G8B8 = 0x08


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def fwd(p):
    return p.replace(os.sep, '/')


def rmtree(path):
    def retry(func, p, _):
        os.chmod(p, 0o644)
        func(p)
    shutil.rmtree(path, onerror=retry)


def hexint(v):
    return int(v, 16) if isinstance(v, str) else int(v)


class Trace:
    """Follows the trace log while xemu writes it."""

    def __init__(self, path):
        self.path = path
        self.pos = 0
        self.carry = b''
        self.flip_pos = []      # byte offset of each flip line
        self.flip_new = []      # write3d after each flip
        self.flip_time = []     # host time when the line was seen
        self.flip_vbl = []      # vblanks seen by the guest up to the flip
        self.vblanks = 0        # rising edges of the PCRTC bit in nv2a_irq
        self.pcrtc = False

    @property
    def flips(self):
        return len(self.flip_pos)

    def update(self):
        try:
            with open(self.path, 'rb') as f:
                f.seek(self.pos)
                data = f.read()
        except OSError:
            return
        if not data:
            return
        now = time.monotonic()
        base = self.pos - len(self.carry)
        buf = self.carry + data
        self.pos += len(data)
        end = buf.rfind(b'\n') + 1
        self.carry = buf[end:]
        i = 0
        while i < end:
            j = buf.index(b'\n', i) + 1
            line = buf[i:j]
            if line.startswith(IRQ):
                self.irq(line)
            elif line.startswith(FLIP):
                m = FLIP_RE.match(line)
                self.flip_pos.append(base + i)
                self.flip_new.append(int(m.group(2), 16))
                self.flip_time.append(now)
                self.flip_vbl.append(self.vblanks)
            i = j

    def irq(self, line):
        on = bool(int(line[len(IRQ):].strip(), 16) & NV_PMC_INTR_0_PCRTC)
        if on and not self.pcrtc:
            self.vblanks += 1
        self.pcrtc = on

    def read(self, start, stop=None):
        with open(self.path, 'rb') as f:
            f.seek(start)
            return f.read(-1 if stop is None else stop - start)

    def line_end(self, pos):
        return pos + len(self.read(pos, pos + 512).split(b'\n', 1)[0]) + 1


def window_title(pid):
    u = ctypes.windll.user32
    out = []

    @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    def cb(h, _):
        p = W.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h):
            b = ctypes.create_unicode_buffer(256)
            u.GetWindowTextW(h, b, 256)
            if b.value:
                out.append(b.value)
        return True
    u.EnumWindows(cb, 0)
    return out[0] if out else None


class Audio:
    """Records the host loopback input while xemu runs. Not bit exact."""

    def __init__(self):
        import sounddevice as sd
        devs = [i for i, d in enumerate(sd.query_devices())
                if 'Stereomix' in d['name']
                and sd.query_hostapis(d['hostapi'])['name'] == 'Windows WASAPI']
        if not devs:
            raise RuntimeError('no WASAPI Stereomix input found')
        self.chunks = []
        self.info = {'device': sd.query_devices(devs[0])['name'],
                     'rate': 48000, 'channels': 2, 'format': 's16le',
                     'overflows': 0}

        def cb(data, frames, t, status):
            if status.input_overflow:
                self.info['overflows'] += 1
            self.chunks.append(data.copy())
        self.s = sd.InputStream(device=devs[0], channels=2, samplerate=48000,
                                dtype='int16', callback=cb)
        self.s.start()
        self.t_start = time.monotonic()

    def save(self, path):
        import wave
        import numpy as np
        self.s.stop()
        a = (np.concatenate(self.chunks) if self.chunks
             else np.zeros((0, 2), np.int16))
        with wave.open(path, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(48000)
            w.writeframes(a.tobytes())
        self.info['frames'] = int(a.shape[0])
        self.info['peak'] = int(abs(a.astype(int)).max()) if a.size else 0


class Bench:
    def __init__(self, a):
        self.a = a
        self.dir = os.path.join(RUNS, a.name)
        if os.path.exists(self.dir):
            rmtree(self.dir)
        os.makedirs(self.dir)
        self.image = os.path.join(self.dir, 'snap.qcow2')
        self.log = {'args': vars(a), 'snapshots': [], 'notes': []}

    # -- run ---------------------------------------------------------------

    def command(self):
        d = self.dir
        if not os.path.exists(EEPROM):
            sys.exit(f'missing {EEPROM}; create it once with --make-eeprom')
        shutil.copyfile(EEPROM, os.path.join(d, 'eeprom.bin'))
        with open(os.path.join(d, 'xemu.toml'), 'w') as f:
            f.write(config(os.path.join(d, 'eeprom.bin'), getattr(self.a, 'dsp', False)))
        with open(self.image, 'wb') as f:
            f.write(qcow2.build(RAM_SIZE))
        xemu = XEMU[self.a.xemu]
        self.log['inputs'] = {
            'xemu': {'path': fwd(os.path.relpath(xemu, ROOT)), 'sha256': sha256(xemu)},
            'mcpx': sha256(MCPX), 'flash': sha256(FLASH), 'eeprom': sha256(EEPROM)}
        args = [xemu, '-config_path', os.path.join(d, 'xemu.toml'), '-S',
                '-qmp', f'tcp:127.0.0.1:{QMP_PORT},server,nowait',
                '-blockdev', 'driver=qcow2,node-name=snap,file.driver=file,'
                'file.filename=' + fwd(self.image),
                '-D', os.path.join(d, 'trace.log')]
        if self.a.brk or getattr(self.a, 'patch', None):
            args += ['-gdb', f'tcp:127.0.0.1:{GDB_PORT}']
        for e in TRACE_ALWAYS + list(getattr(self.a, 'trace', None) or []):
            args += ['-trace', 'enable=' + e]
        self.log['command'] = [fwd(os.path.relpath(x, ROOT)) if os.path.isabs(x) else x
                               for x in args]
        return args

    def start(self):
        args = self.command()
        self.stderr = open(os.path.join(self.dir, 'stderr.txt'), 'w')
        self.audio = Audio() if self.a.audio else None
        self.p = subprocess.Popen(args, stdout=self.stderr, stderr=subprocess.STDOUT)
        self.q = QMP(QMP_PORT)
        self.tr = Trace(os.path.join(self.dir, 'trace.log'))
        self.ff = None
        if self.a.video:
            t = None
            for _ in range(200):
                t = window_title(self.p.pid)
                if t:
                    break
                time.sleep(0.05)
            self.ff = subprocess.Popen(
                ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y',
                 '-f', 'gdigrab', '-framerate', '60', '-i', 'title=' + t,
                 '-c:v', 'ffv1', os.path.join(self.dir, 'video.mkv')],
                stdin=subprocess.PIPE)
        self.t0 = time.monotonic()

    def quit(self):
        if self.ff:
            try:
                self.ff.communicate(b'q', timeout=30)
            except subprocess.TimeoutExpired:
                self.ff.kill()
        try:
            # After a savevm at a breakpoint xemu does not answer quit.
            self.q.sock.settimeout(10)
            self.q.cmd('quit')
        except Exception:
            pass
        try:
            self.p.wait(20)
        except subprocess.TimeoutExpired:
            self.p.kill()
            self.log['notes'].append('xemu did not quit within 20 s, killed')
        self.log['xemu_exit'] = self.p.returncode
        self.tr.update()
        if self.audio:
            self.audio.save(os.path.join(self.dir, 'audio.wav'))
            self.log['audio'] = self.audio.info
            self.log['audio']['start_ms'] = round((self.audio.t_start - self.t0) * 1000, 1)
        with open(os.path.join(self.dir, 'flips.json'), 'w') as f:
            json.dump([{'n': i + 1, 'write3d': w, 'vblanks': v,
                        't_ms': round((t - self.t0) * 1000, 1)}
                       for i, (w, v, t) in enumerate(zip(self.tr.flip_new, self.tr.flip_vbl,
                                                         self.tr.flip_time))], f)
        self.log['flips_total'] = self.tr.flips

    def wait_flips(self, n, near=None):
        while self.tr.flips < n:
            self.tr.update()
            if near and self.tr.flips >= n - 2:
                near()
                near = None
            if self.p.poll() is not None:
                raise RuntimeError(f'xemu exited before flip {n}')
            if time.monotonic() - self.t0 > self.a.timeout:
                raise RuntimeError(f'timeout before flip {n}')
            time.sleep(0.001)

    def arm(self, methods):
        self.q.cmd('trace-event-set-state', name=DOWNLOAD, enable=True)
        if methods:
            self.methods(True)

    def methods(self, on):
        self.q.cmd('trace-event-set-state', name=TRACE_METHODS, enable=on)

    def savevm(self, name, kind, target=None):
        """Snapshot while running; the save stops and resumes the VM.

        nv2a_pgraph_surface_download is on from two flips before (see
        arm) until the save returned. The
        dirty-surface write-back of the save runs on the PFIFO thread after
        the FIFO is halted, so its lines follow every flip that happened
        before the stop and precede every flip after the resume.
        """
        n_seen, pos = self.tr.flips, self.tr.pos
        vbl, pcrtc = self.tr.vblanks, self.tr.pcrtc
        t = time.monotonic()
        r = self.q.hmp(f'savevm {name}')
        dt = time.monotonic() - t
        self.q.cmd('trace-event-set-state', name=DOWNLOAD, enable=False)
        time.sleep(0.02)
        self.tr.update()
        s = {'name': name, 'kind': kind, 'target': target,
             'flips_seen': n_seen, 'flips_after': self.tr.flips,
             'vblanks_seen': vbl, 'pcrtc_seen': pcrtc,
             'trace_pos': pos, 'trace_end': self.tr.pos,
             't_ms': round((t - self.t0) * 1000, 1),
             'savevm_ms': round(dt * 1000, 1)}
        if r.strip():
            s['savevm_msg'] = r.strip()
        self.log['snapshots'].append(s)
        return s

    # -- after the run -----------------------------------------------------

    def unpack(self, s):
        full = s['kind'] in ('frame', 'break')
        tmp = os.path.join(self.dir, '_unpack')
        state = vmstate.extract(self.image, s['name'], tmp)
        nv2a = next(v for k, v in state.items() if '/nv2a (' in k)
        cpu = next(v for k, v in state.items() if k.startswith('cpu ('))
        self.which_flip(s, nv2a)
        got = s['frame'] if s['frame'] is not None else s['target']
        if s['kind'] == 'frame':
            out = os.path.join(self.dir, f"frame_{got:04d}")
        elif s['kind'] == 'break':
            out = os.path.join(self.dir, s['name'])
        else:
            out = os.path.join(self.dir, 'sequence')
        os.makedirs(out, exist_ok=True)
        ram = open(os.path.join(tmp, 'xbox.ram'), 'rb').read()
        png = (os.path.join(out, 'frame.png') if full
               else os.path.join(out, f"{got:04d}.png"))
        s.update(self.framebuffer(ram, nv2a, s, png))
        if self.a.peek:
            # kernel addresses map to RAM at their low 31 bits
            s['peek'] = {f'{addr:#010x}': ram[addr & 0x7FFFFFFF:(addr & 0x7FFFFFFF) + n].hex()
                         for addr, n in self.a.peek}
        if full:
            shutil.move(os.path.join(tmp, 'xbox.ram'), os.path.join(out, 'ram.bin'))
            shutil.move(os.path.join(tmp, 'pci.0_1e.0_00.0', 'nv2a-ramin'),
                        os.path.join(out, 'pramin.bin'))
            shutil.move(os.path.join(tmp, 'state.json'), os.path.join(out, 'state.json'))
            with open(os.path.join(out, 'cpu.json'), 'w') as f:
                json.dump(cpu, f, indent=1)
            s['ram_sha256'] = sha256(os.path.join(out, 'ram.bin'))
            if s['kind'] == 'frame' and s.get('frame'):
                s['methods'] = self.methods_of_frame(s['frame'], out)
        s['eip'] = cpu['env.eip']
        rmtree(tmp)

    def which_flip(self, s, nv2a):
        reg = hexint(nv2a['pgraph.regs_'][NV_PGRAPH_SURFACE])
        w = (reg >> SURFACE_WRITE_3D[0]) & SURFACE_WRITE_3D[1]
        mod = (reg >> SURFACE_MODULO_3D[0]) & SURFACE_MODULO_3D[1]
        s['write3d'], s['modulo3d'] = w, mod
        # Runs of download lines inside the save window, each with the
        # number of flips and of guest vblanks logged before it.
        runs, flips, prev = [], s['flips_seen'], None
        edge = Trace(None)
        edge.vblanks, edge.pcrtc = s['vblanks_seen'], s['pcrtc_seen']
        for line in self.tr.read(s['trace_pos'], s['trace_end']).split(b'\n'):
            if line.startswith(FLIP):
                flips += 1
                prev = None
            elif line.startswith(IRQ):
                edge.irq(line)
            elif line.startswith(DOWNLOAD.encode()):
                if prev is None:
                    runs.append([flips, 0, edge.vblanks])
                    prev = runs[-1]
                prev[1] += 1
        s['download_runs'] = runs

        def parity(n):
            return (self.tr.flip_new[n - 1] if n else 0) == w
        cand = sorted({r[0] for r in runs if parity(r[0])})
        s['frame_candidates'] = cand
        s['vblanks'] = next((r[2] for r in runs if r[0] in cand), None)
        if len(cand) == 1:
            s['frame'], s['frame_rule'] = cand[0], 'download run + write3d'
        elif cand:
            n = max((r for r in runs if r[0] in cand), key=lambda r: r[1])[0]
            s['frame'], s['frame_rule'] = n, 'longest download run + write3d'
        else:
            s['frame'], s['frame_rule'] = None, 'no download run matches write3d'

    def framebuffer(self, ram, nv2a, s, png):
        from PIL import Image
        import numpy as np
        start = hexint(nv2a['pcrtc.start'])
        info = {'pcrtc_start': f'{start:#x}'}
        geo = [m for m in CREATE_RE.findall(self.tr.read(0, s['trace_pos']))
               if int(m[0], 16) == start]
        fmt = hexint(nv2a['pgraph.surface_shape.color_format'])
        fmt_at = hexint(nv2a['pgraph.surface_color.offset'])
        if not geo:
            info['fb_note'] = 'no surface created at PCRTC start'
            return info
        _, w, h, layout, p = geo[-1]
        w, h, p = int(w), int(h), int(p)
        info['fb_geometry'] = [w, h, p]
        info['fb_format'] = (f'{fmt:#x} (current PGRAPH color target)'
                             if fmt_at == start else 'not the current target, assumed 0x8')
        if layout.decode() not in ('ln', 'lin') or (fmt_at == start and fmt != COLOR_LE_A8R8G8B8):
            info['fb_note'] = f'unsupported layout {layout.decode()} or format {fmt:#x}'
            return info
        raw = ram[start:start + p * h]
        a = np.frombuffer(raw, np.uint8).reshape(h, p)[:, :w * 4].reshape(h, w, 4)
        Image.fromarray(a[:, :, 2::-1].copy()).save(png)
        info['fb_sha256'] = hashlib.sha256(raw).hexdigest()
        info['png'] = fwd(os.path.relpath(png, self.dir))
        return info

    def methods_of_frame(self, n, out):
        """PGRAPH methods after flip n-1 up to the line of flip n.

        The method line of FLIP_INCREMENT_WRITE is logged before its
        handler runs, so it is the last method line before flip n.
        """
        start = self.tr.line_end(self.tr.flip_pos[n - 2]) if n >= 2 else 0
        chunk = self.tr.read(start, self.tr.flip_pos[n - 1])
        lines = [x for x in chunk.split(b'\n') if x.startswith(b'nv2a_pgraph_method')]
        with open(os.path.join(out, 'methods.txt'), 'wb') as f:
            f.write(b'\n'.join(lines) + b'\n')
        return len(lines)

    def finish(self):
        for s in self.log['snapshots']:
            t = time.monotonic()
            try:
                self.unpack(s)
            except Exception as e:
                s['unpack_error'] = repr(e)
            s['unpack_s'] = round(time.monotonic() - t, 1)
            print(json.dumps({k: v for k, v in s.items() if k not in ('trace_pos', 'trace_end', 'pcrtc_seen')}))
        if any('unpack_error' in s for s in self.log['snapshots']):
            self.log['notes'].append('unpack failed, snap.qcow2 kept')
        elif not self.a.keep_image:
            os.remove(self.image)
        with open(os.path.join(self.dir, 'run.json'), 'w') as f:
            json.dump(self.log, f, indent=1)


def config(eeprom, dsp=False):
    """dsp: audio.use_dsp; off (xemu's default) plays the VP monitor mix,
    on plays the output of the GP/EP DSPs (hw/xbox/mcpx/apu/dsp/gp_ep.c)."""
    return ('[general]\nshow_welcome = false\n\n'
            + ('[audio]\nuse_dsp = true\n\n' if dsp else '') +
            '[display]\nsetup_nvidia_profile = false\n\n'
            '[perf]\ncache_shaders = false\n\n'
            '[sys.files]\n'
            f"bootrom_path = '{fwd(MCPX)}'\n"
            f"flashrom_path = '{fwd(FLASH)}'\n"
            f"eeprom_path = '{fwd(eeprom)}'\n")


def make_eeprom():
    """Start xemu once without an EEPROM so it generates one, keep it."""
    tmp = os.path.join(ROOT, 'out', 'xemu', 'eeprom_gen')
    if os.path.exists(tmp):
        rmtree(tmp)
    os.makedirs(tmp)
    eep = os.path.join(tmp, 'eeprom.bin')
    with open(os.path.join(tmp, 'xemu.toml'), 'w') as f:
        f.write(config(eep))
    p = subprocess.Popen([XEMU['release'], '-config_path', os.path.join(tmp, 'xemu.toml'),
                          '-S', '-qmp', f'tcp:127.0.0.1:{QMP_PORT},server,nowait'],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    QMP(QMP_PORT).cmd('quit')
    p.wait(20)
    shutil.copyfile(eep, EEPROM)
    print('eeprom', EEPROM, sha256(EEPROM))


def patch_kernel(patches):
    """Stops at the kernel entry over the GDB stub, writes the patches and
    reads them back, then lets the guest run on. The kernel image is in RAM
    there (the 2BL unpacked it), and none of it has run yet."""
    g = GDB(GDB_PORT)
    g.hbreak(KERNEL_ENTRY)
    g.cont()
    g.wait_stop()
    g.hbreak(KERNEL_ENTRY, False)
    done = []
    for p in patches:
        addr, data = p.split(':')
        addr, data = int(addr, 0), bytes.fromhex(data)
        old = g.read(addr, len(data))
        g.write(addr, data)
        if g.read(addr, len(data)) != data:
            raise RuntimeError(f'patch at {addr:#x} did not stick')
        done.append({'addr': f'{addr:#010x}', 'old': old.hex(), 'new': data.hex()})
    g.cont()
    g.close()
    return done


def parse_frames(spec):
    a, b, s = (int(x) for x in spec.split(':'))
    return list(range(a, b + 1, s))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--name', default=time.strftime('%Y%m%d-%H%M%S'))
    ap.add_argument('--frame', type=int, action='append', default=[],
                    help='capture everything after this flip (repeatable)')
    ap.add_argument('--frames', help='A:B:S, the scanned-out image every S flips')
    ap.add_argument('--break', dest='brk',
                    help='hardware breakpoint at ADDR; captured, then the run ends')
    ap.add_argument('--video', action='store_true')
    ap.add_argument('--audio', action='store_true')
    ap.add_argument('--duration', type=float, default=0,
                    help='keep running until this many seconds after start')
    ap.add_argument('--xemu', choices=XEMU, default='release')
    ap.add_argument('--timeout', type=float, default=120)
    ap.add_argument('--keep-image', action='store_true', help='keep snap.qcow2')
    ap.add_argument('--make-eeprom', action='store_true')
    ap.add_argument('--dsp', action='store_true',
                    help='audio.use_dsp = true: play the DSP output instead of the VP monitor mix')
    ap.add_argument('--trace', action='append', default=[],
                    help='enable this trace event too, e.g. mcpx_apu_method (repeatable)')
    ap.add_argument('--patch', action='append', default=[],
                    help='ADDR:HEX - overwrite guest code at the kernel entry '
                         f'({KERNEL_ENTRY:#x}), before the kernel runs (repeatable)')
    ap.add_argument('--peek', action='append', default=[],
                    type=lambda v: tuple(int(x, 0) for x in v.split(':')),
                    help='ADDR:LEN - keep these bytes of guest RAM from every snapshot '
                         'in run.json (repeatable)')
    a = ap.parse_args()
    if a.make_eeprom:
        return make_eeprom()
    if any(n < 1 for n in a.frame):
        ap.error('--frame counts flips from 1')

    b = Bench(a)
    b.start()
    try:
        if a.brk:
            addr = int(a.brk, 0)
            g = GDB(GDB_PORT)
            g.hbreak(addr)
            g.cont()
            g.wait_stop()
            g.hbreak(addr, False)
            b.log['break_regs_gdb'] = {k: f'{v:#010x}' for k, v in g.regs().items()}
            b.savevm(f'break_{addr:08x}', 'break')
            g.close()
        else:
            todo = sorted({(n, 'frame') for n in a.frame} |
                          {(n, 'seq') for n in (parse_frames(a.frames) if a.frames else [])
                           if n >= 1 and n not in a.frame})
            if a.patch:
                b.log['patches'] = patch_kernel(a.patch)
            b.q.cmd('cont')
            for n, kind in todo:
                if b.tr.flips >= n:
                    b.log['notes'].append(f'flip {n} passed before it could be captured')
                    continue
                try:
                    b.wait_flips(n, lambda: b.arm(kind == 'frame'))
                except RuntimeError as e:
                    # keep what was captured: the guest stops flipping when
                    # the animation ends
                    b.log['notes'].append(f'{e}; {b.tr.flips} flips seen')
                    break
                b.savevm(f'{kind}{n:04d}', kind, n)
                if kind == 'frame':
                    b.methods(False)
            while time.monotonic() - b.t0 < a.duration:
                b.tr.update()
                time.sleep(0.05)
    finally:
        b.quit()
    b.finish()
    print('run', b.dir)


if __name__ == '__main__':
    main()
