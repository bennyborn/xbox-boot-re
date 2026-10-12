"""Write an empty qcow2 image.

xemu only writes dirty GPU surfaces back to guest RAM when the machine
enters the save-VM state (hw/xbox/nv2a/nv2a.c, nv2a_vm_state_change), and
QEMU's savevm needs a block device that can hold internal snapshots. This
image is that device; it is attached with if=none, so the guest never sees
it.

Layout after docs/interop/qcow2.rst of xemu (version 3, 64 KiB clusters,
16-bit refcounts):

    cluster 0  header
    cluster 1  refcount table, one entry -> cluster 2
    cluster 2  refcount block, clusters 0..3 used once
    cluster 3  L1 table, all entries unallocated
"""

import argparse
import struct

CLUSTER_BITS = 16
CLUSTER = 1 << CLUSTER_BITS


def build(size):
    l2_entries = CLUSTER // 8
    l1_size = -(-size // (CLUSTER * l2_entries))
    assert l1_size * 8 <= CLUSTER

    header = struct.pack(
        '>4sIQIIQIIQQIIQQQQII',
        b'QFI\xfb', 3,          # magic, version
        0, 0,                   # no backing file
        CLUSTER_BITS, size,
        0,                      # crypt_method
        l1_size, 3 * CLUSTER,   # L1 table in cluster 3
        1 * CLUSTER, 1,         # refcount table in cluster 1, one cluster
        0, 0,                   # no snapshots yet
        0, 0, 0,                # incompatible, compatible, autoclear
        4, 104)                 # refcount_order (16 bit), header_length
    assert len(header) == 104
    # Header extensions follow; a zero type ends them.
    c0 = header + b'\0' * 8
    c1 = struct.pack('>Q', 2 * CLUSTER)
    c2 = struct.pack('>4H', 1, 1, 1, 1)
    c3 = b''
    return b''.join(c.ljust(CLUSTER, b'\0') for c in (c0, c1, c2, c3))


OFFSET_MASK = ((1 << 56) - 1) & ~((1 << 9) - 1)     # bits 9..55


def snapshots(path):
    """Snapshot table: list of dicts with name, l1 offset/size, vm state size."""
    with open(path, 'rb') as f:
        h = f.read(104)
        (magic, version, _, _, cluster_bits, size, _, l1_size, _, _, _,
         nb, snap_off) = struct.unpack_from('>4sIQIIQIIQQIIQ', h)
        assert magic == b'QFI\xfb'
        out = []
        f.seek(snap_off)
        for _ in range(nb):
            e = f.read(40)
            l1_off, l1_n, id_len, name_len, _, _, _, vm32, extra = \
                struct.unpack('>QIHHIIQII', e)
            x = f.read(extra)
            vm = struct.unpack_from('>Q', x, 0)[0] if extra >= 8 else vm32
            sid = f.read(id_len).decode()
            name = f.read(name_len).decode()
            f.read(-(40 + extra + id_len + name_len) % 8)
            out.append({'id': sid, 'name': name, 'l1_offset': l1_off,
                        'l1_size': l1_n, 'vm_state_size': vm})
        return out, cluster_bits, size


def read_vmstate(path, name):
    """The saved VM state (a QEMU migration stream) of one snapshot.

    The state is stored like disk content at guest offsets from the first
    L1 entry that no longer describes the virtual disk (qcow2.rst,
    "Snapshot table entry", bytes 32-35).
    """
    snaps, cluster_bits, size = snapshots(path)
    s = [x for x in snaps if x['name'] == name]
    if not s:
        raise KeyError(f'no snapshot {name!r} in {path}')
    s = s[-1]
    cluster = 1 << cluster_bits
    l2_entries = cluster // 8
    per_l1 = cluster * l2_entries
    start = -(-size // per_l1) * per_l1
    out = bytearray()
    with open(path, 'rb') as f:
        f.seek(s['l1_offset'])
        l1 = struct.unpack(f">{s['l1_size']}Q", f.read(8 * s['l1_size']))
        pos = start
        while len(out) < s['vm_state_size']:
            l2_off = l1[pos // per_l1] & OFFSET_MASK
            n = min(cluster, s['vm_state_size'] - len(out))
            data = b'\0' * n
            if l2_off:
                f.seek(l2_off + 8 * ((pos % per_l1) // cluster))
                e, = struct.unpack('>Q', f.read(8))
                assert not e & (1 << 62), 'compressed cluster'
                if e & OFFSET_MASK and not e & 1:
                    f.seek(e & OFFSET_MASK)
                    data = f.read(n)
            out += data
            pos += cluster
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('path')
    ap.add_argument('--size', type=int, default=64 << 20)
    a = ap.parse_args()
    with open(a.path, 'wb') as f:
        f.write(build(a.size))


if __name__ == '__main__':
    main()
