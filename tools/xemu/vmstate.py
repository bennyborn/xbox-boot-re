"""Unpack a savevm snapshot of xemu into RAM files and a device state JSON.

The snapshot's VM state is a QEMU migration stream behind a small header
that xemu writes first (ui/xemu-snapshots.c, xemu_snapshots_save_extra_data:
be32 magic, be32 version, be32 size, then size bytes). The stream itself is
parsed by scripts/analyze-migration.py from the xemu source, kept unchanged
in .tools/qemu-scripts/. Two things are replaced in the loaded copy: the
file names it derives from RAM block names, because Windows does not allow
':' in them, and the reading of integer arrays (see _fast_arrays). The
resulting state is the same; checked on a snapshot of 02.10.2026, where
both ways gave identical state.json bytes (12.1 s against 5.5 s).
state.json is written without indentation.
"""

import builtins
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qcow2  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ANALYZE = os.path.join(ROOT, '.tools', 'qemu-scripts', 'analyze-migration.py')
XEMU_MAGIC = b'xemu'


def strip_xemu_header(v):
    if v[:4] != XEMU_MAGIC:
        return v
    n, = struct.unpack_from('>I', v, 8)
    s = v[12 + n:]
    if s[:4] != b'QEVM':
        raise ValueError('no QEVM magic after the xemu header')
    return s


def _load_analyzer():
    src = open(ANALYZE, encoding='utf-8').read()
    cut = src.index('parser = argparse.ArgumentParser()')
    ns = {'__name__': 'analyze_migration'}

    def safe(p):
        return p.replace(':', '_')

    def _open(p, *a, **k):
        return builtins.open(safe(p) if isinstance(p, str) else p, *a, **k)

    exec(compile(src[:cut], ANALYZE, 'exec'), ns)
    ns['open'] = _open
    mk = ns['mkdir_p']
    ns['mkdir_p'] = lambda p: mk(safe(p))
    _fast_arrays(ns)
    return ns


INT_TYPES = {'int8', 'int16', 'int32', 'int32 equal', 'int64', 'int64 equal',
             'uint8 equal', 'uint16 equal'}
UINT_TYPES = {'uint8', 'uint16', 'uint32', 'uint32 equal', 'uint64'}


def _fast_arrays(ns):
    """Read runs of integer array elements in one go.

    VMSDFieldStruct.read creates one reader object per array element; the
    NV2A state has over a million of them. This replacement reads a run of
    elements of one integer field at once and stores the same strings the
    original getDict would produce, so state.json does not change. Every
    other field goes through the original readers.
    """
    import numpy as np
    Struct = ns['VMSDFieldStruct']
    original = Struct.read

    def read(self):
        fields = self.desc['struct']['fields']
        if not any('index' in f and f['type'] in INT_TYPES | UINT_TYPES for f in fields):
            return original(self)
        readers = ns['vmsd_field_readers']
        i = 0
        while i < len(fields):
            f = fields[i]
            t, name = f['type'], f['name']
            if 'index' in f and (t in INT_TYPES or t in UINT_TYPES):
                j = i
                while (j < len(fields) and fields[j]['name'] == name
                       and fields[j]['type'] == t and 'index' in fields[j]):
                    j += 1
                size = int(f['size'])
                n = j - i
                raw = self.file.readvar(size * n)
                u = np.frombuffer(raw, f'>u{size}').tolist()
                fmt = '0x%%0%dx' % (size * 2)
                if t in INT_TYPES:
                    s = np.frombuffer(raw, f'>i{size}').tolist()
                    vals = [fmt % a if b >= 0 else '%s (%d)' % (fmt % a, b)
                            for a, b in zip(u, s)]
                else:
                    vals = [fmt % a for a in u]
                old = self.data.get(name)
                if old is None:
                    self.data[name] = vals if n > 1 else vals[0]
                elif isinstance(old, list):
                    old.extend(vals)
                else:
                    self.data[name] = [old] + vals
                i = j
                continue
            reader = readers.get(t, ns['VMSDFieldGeneric'])
            f['data'] = reader(f, self.file)
            f['data'].read()
            old = self.data.get(name)
            if old is None:
                self.data[name] = f['data']
            elif isinstance(old, list):
                old.append(f['data'])
            else:
                self.data[name] = [old, f['data']]
            i += 1
        if 'subsections' in self.desc['struct']:
            for sub in self.desc['struct']['subsections']:
                if self.file.read8() != self.QEMU_VM_SUBSECTION:
                    raise Exception('Subsection %s not found at offset %x'
                                    % (sub['vmsd_name'], self.file.tell()))
                sname = self.file.readstr()
                version_id = self.file.read32()
                self.data[sname] = ns['VMSDSection'](self.file, version_id, sub, (sname, 0))
                self.data[sname].read()

    Struct.read = read


def extract(image, snapshot, outdir):
    """Write outdir/stream.bin, the RAM blocks and outdir/state.json."""
    os.makedirs(outdir, exist_ok=True)
    stream = os.path.join(outdir, 'stream.bin')
    with open(stream, 'wb') as f:
        f.write(strip_xemu_header(qcow2.read_vmstate(image, snapshot)))
    ns = _load_analyzer()
    cwd = os.getcwd()
    os.chdir(outdir)
    try:
        dump = ns['MigrationDump']('stream.bin')
        dump.read(write_memory=True)
        state = dump.getDict()
        with open('state.json', 'w') as f:
            text = ns['JSONEncoder'](separators=(',', ':')).encode(state)
            f.write(text)
    finally:
        os.chdir(cwd)
    os.remove(stream)
    return json.loads(text)


if __name__ == '__main__':
    extract(sys.argv[1], sys.argv[2], sys.argv[3])
