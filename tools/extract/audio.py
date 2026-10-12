"""Extracts the data of the start-up sound from the unpacked kernel.

    python tools/extract/audio.py

Reads out/rom/kernel.img (tools/rom/unpack.py) and writes data/audio/:

  sequence.bin     0x8006E908-0x8006EFFF: sequencer header, per-channel
                   pointer tables, sample descriptors, track bytecode and
                   event records, as they stand in the image
  sequence.json    the same, decoded: header, events, descriptors, the
                   track lists of the events and every track as a list of
                   commands (format: docs/decomp/Audio.md, section 3)
  envblocks.bin    0x800A7980-0x800A7BAF: the 14 parameter blocks of 0x28
                   bytes that the descriptors point to
  envblocks.json   the same as lists of ten 32-bit words
  seq_pointers.bin 0x800A7BB0-0x800A7C13: the eleven pointers to the sample
                   descriptors (index = sample number), the track lists of
                   events 1 and 2 and the highest event number, as they
                   stand in the image; sequence.json holds them decoded
                   (descriptors, event_lists, event_max). In data/ since K7,
                   because the core reads the pack and not the .json
  pcm_80066ac0.raw 0x5540 signed 8-bit samples
  pcm_8006c008.raw 0xEB8 unsigned 8-bit samples (0x80 = zero)
  pcm_8006cec8.raw 0x1A3F unsigned 8-bit samples (0x80 = zero)
  gp_program.bin   0x80053568-0x80054DFB: the program of the GP DSP, 0x625
                   words of 32 bits (24 used); 0x8004F8A1 hands address and
                   length 0x1894 to the loader

The tables that the kernel computes at run time (noise, sine, saw, FM
tone) are not data of the ROM; their formulas are in Audio.md and
tools/xemu/audio_model.py builds them.
"""

import hashlib
import json
import os
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BASE = 0x80010000
OUT = os.path.join(ROOT, 'data', 'audio')

SEQ = (0x8006E908, 0x8006F000)
ENV = (0x800A7980, 0x800A7BB0)
PTRS = (0x800A7BB0, 0x800A7C14)
GP_PROGRAM = (0x80053568, 0x1894)
PCM = [(0x80066AC0, 0x5540, 'signed'), (0x8006C008, 0xEB8, 'unsigned'),
       (0x8006CEC8, 0x1A3F, 'unsigned')]

HEADER = 0x8006E908          # u16 track records, u16 channels
EVENTS = 0x8006EFE0          # 8-byte records
EVENT_MAX = 0x800A7C10       # highest valid event number (dword)
DESC_PTRS = 0x800A7BB0       # 11 pointers to 0x14-byte sample descriptors
N_DESC = 11

# command number -> (name, operand layout); see Audio.md section 3
OPS = {0: ('rest', 'h'), 1: ('note', 'Bxh'), 2: ('jump', 'I'), 3: ('loop', 'H'),
       4: ('repeat', ''), 5: ('sample', 'H'), 6: ('set_b2', 'H'), 9: ('vol_add', 'h'),
       10: ('transpose_add', 'h'), 11: ('transpose_set', 'h'), 12: ('glide', 'Bxh'),
       13: ('wait', 'h'), 15: ('end', ''), 16: ('filter_add', 'hH'), 17: ('filter_set', 'HH')}


class Image:
    def __init__(self, path):
        self.b = open(path, 'rb').read()

    def rd(self, a, n):
        return self.b[a - BASE:a - BASE + n]

    def u(self, fmt, a):
        return struct.unpack('<' + fmt, self.rd(a, struct.calcsize('<' + fmt)))


def decode_track(img, start, stop):
    out, a = [], start
    while a < stop:
        op = img.u('H', a)[0]
        if op not in OPS:
            raise ValueError('unknown command %d at %#x' % (op, a))
        name, fmt = OPS[op]
        args = list(img.u(fmt, a + 2)) if fmt else []
        out.append({'addr': '%#010x' % a, 'op': op, 'name': name,
                    'args': ['%#010x' % x for x in args] if op == 2 else args})
        a += 2 + struct.calcsize('<' + fmt)
    return out


def main():
    img = Image(os.path.join(ROOT, 'out', 'rom', 'kernel.img'))
    os.makedirs(OUT, exist_ok=True)
    files = {}

    def write(name, data):
        with open(os.path.join(OUT, name), 'wb') as f:
            f.write(data)
        files[name] = hashlib.sha256(data).hexdigest()

    write('sequence.bin', img.rd(SEQ[0], SEQ[1] - SEQ[0]))
    write('envblocks.bin', img.rd(ENV[0], ENV[1] - ENV[0]))
    write('seq_pointers.bin', img.rd(PTRS[0], PTRS[1] - PTRS[0]))
    write('gp_program.bin', img.rd(GP_PROGRAM[0], GP_PROGRAM[1]))
    for a, n, kind in PCM:
        write('pcm_%08x.raw' % a, img.rd(a, n))

    n_tracks, n_channels = img.u('HH', HEADER)
    ev_max = img.u('I', EVENT_MAX)[0]
    events = []
    for e in range(ev_max + 1):
        handler, mask, lst = img.u('HHI', EVENTS + 8 * e)
        events.append({'event': e, 'handler': handler, 'mask': '%#06x' % mask,
                       'list': '%#010x' % lst})
    descs = []
    for i in range(N_DESC):
        p = img.u('I', DESC_PTRS + 4 * i)[0]
        data, length, w8, wa, e0, e1 = img.u('IIHHII', p)
        descs.append({'index': i, 'at': '%#010x' % p, 'data': '%#010x' % data,
                      'bytes': length, 'w8': w8, 'loop': wa,
                      'block_a': '%#010x' % e0, 'block_b': '%#010x' % e1})
    lists = {}
    for ev in events:
        if ev['handler'] in (1, 2):
            mask = int(ev['mask'], 16)
            ptrs = [img.u('I', int(ev['list'], 16) + 4 * k)[0] for k in range(bin(mask).count('1'))]
            lists[ev['list']] = ['%#010x' % p for p in ptrs]
    starts = sorted({int(p, 16) for v in lists.values() for p in v})
    bounds = starts + [EVENTS]
    tracks = {}
    for s in starts:
        tracks['%#010x' % s] = decode_track(img, s, min(b for b in bounds if b > s))
    blocks = []
    for a in range(ENV[0], ENV[1], 0x28):
        blocks.append({'at': '%#010x' % a, 'words': [x - (1 << 32) if x >> 31 else x
                                                     for x in img.u('10I', a)]})

    seq = {'source': 'out/rom/kernel.img, %#010x-%#010x' % SEQ,
           'track_records': n_tracks, 'channels': n_channels, 'event_max': ev_max,
           'events': events, 'event_lists': lists, 'descriptors': descs, 'tracks': tracks}
    text = json.dumps(seq, indent=1).encode()
    write('sequence.json', text)
    write('envblocks.json', json.dumps({'source': 'out/rom/kernel.img, %#010x-%#010x' % ENV,
                                        'blocks': blocks}, indent=1).encode())
    with open(os.path.join(OUT, 'SHA256SUMS'), 'w', newline='\n') as f:
        for k in sorted(files):
            f.write('%s  %s\n' % (files[k], k))
    print('%d files in %s' % (len(files), os.path.relpath(OUT, ROOT)))


if __name__ == '__main__':
    main()
