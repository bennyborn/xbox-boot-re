"""Extracts the vertex programs and combiner blocks of the animation.

    python tools/extract/gpu.py

Reads out/rom/kernel.img (tools/rom/unpack.py) and writes

  data/gpu/gpu.json       per vertex program: owner, address, tag, the
                          instruction words, the attribute list the owner
                          builds on the stack, and the disassembly by the
                          third-party tool nv2a_vsh (.tools/pylib); per
                          combiner block: owner, address, the 60 words, the
                          methods 0x80040EF0 sends for it and a decoding of
                          the combiner stages (docs/decomp/GPU.md 3)
  data/gpu/programs.txt   the disassembly once more as plain text

and in gpu.json the table of render-state headers at 0x80065470 that
0x8003CCB0 uses for states 0 to 0x51.

Vertex program (gpu_create_with_decl 0x80056EC1 -> 0x8003FF40): halfword
tag (0x2078), halfword n, then n instructions of four words. 0x8003FC10
sends them in chunks of 32 instructions with method 0xB00
(SET_TRANSFORM_PROGRAM). The attribute list goes to 0x8003FC80 and
0x8003FE40: 0x2000000s selects stream s, 0x40TT00RR puts type TT into
register RR, 0xFFFFFFFF ends it.

Combiner block (gpu_create_from_data 0x80056EDC -> 0x80040E80): word tag
"PSB0" (0x30425350) at the address, then 0x3C words that 0x80040E80 copies.
0x80040EF0 sends them as methods, see BLOCK_METHODS; word 54 is the
texture mode word, words 57-59 map the constants of 0x800410B0 to the
factor registers.
"""

import hashlib
import json
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BASE = 0x80010000
sys.path.insert(0, os.path.join(ROOT, '.tools', 'pylib'))

# owner, field, data address, attribute list, where the list is built
PROGRAMS = [
    ('anim', 0x958, 0x80065B60,
     [0x20000000, 0x40320000, 0x20000001, 0x40320001, 0x40320002, 0x40320003],
     'anim_setup 0x8005768C, stack words local+0x20..+0x38'),
    ('anim', 0x95C, 0x80065998,
     [0x20000000, 0x40320000, 0x20000001, 0x40220001, 0x40320002, 0x40320003, 0x40320004],
     'anim_setup 0x8005768C, stack words local+0x00..+0x18'),
    ('anim', 0x960, 0x80065D28, [0x20000000, 0x40320000],
     'anim_setup 0x8005768C, stack words local+0x3C..+0x40'),
    ('blob', 0x34, 0x800662E8, [0x20000000, 0x40320000, 0x20000001, 0x40420001],
     'blob_setup 0x800598E6'),
    ('blob', 0x38, 0x80066470, [0x20000000, 0x40320000], 'blob_setup 0x800598E6'),
    ('fx', 0x24, 0x80066690, [0x20000000, 0x40320000, 0x40220001, 0x40220002],
     'fx_setup 0x8005A41D'),
    ('logo', 0xCC, 0x80066868, [0x20000000, 0x40320000, 0x40220001], 'logo_setup 0x8005BBEE'),
]

# owner, field, address of the tag word
COMBINERS = [
    ('anim', 0x948, 0x800657A8),
    ('anim', 0x94C, 0x800656B0),
    ('anim', 0x950, 0x800658A0),
    ('blob', 0x3C, 0x800660F8),
    ('blob', 0x40, 0x800661F0),
    ('fx', 0x20, 0x80066598),
    ('logo', 0xC8, 0x80066770),
]

# 0x80040EF0: (first block word, method, count); the last entry only when
# word 8 or word 9 is not zero.
BLOCK_METHODS = [
    (0, 0x260, 8),      # SET_COMBINER_ALPHA_ICW[0..7]
    (10, 0xA60, 8),     # SET_COMBINER_FACTOR0[0..7]
    (18, 0xA80, 8),     # SET_COMBINER_FACTOR1[0..7]
    (26, 0xAA0, 8),     # SET_COMBINER_ALPHA_OCW[0..7]
    (34, 0xAC0, 8),     # SET_COMBINER_COLOR_ICW[0..7]
    (42, 0x17F8, 1),    # SET_SHADER_CLIP_PLANE_MODE
    (43, 0x1E20, 2),    # SET_SPECULAR_FOG_FACTOR[0..1]
    (45, 0x1E40, 8),    # SET_COMBINER_COLOR_OCW[0..7]
    (53, 0x1E60, 1),    # SET_COMBINER_CONTROL
    (55, 0x1E74, 2),    # SET_DOT_RGBMAPPING, SET_SHADER_OTHER_STAGE_INPUT
    (8, 0x288, 2),      # SET_COMBINER_SPECULAR_FOG_CW0/1
]

# 0x8003CCB0: render states 0 to 0x51 go to 0x8003CC60 with the pushbuffer
# header of this table (method in bits 0-12, count in bits 18-28).
RENDER_STATES = (0x80065470, 0x52)

# Register numbers and input mappings of the combiners, after xemu
# hw/xbox/nv2a/pgraph/psh_regs.h; names are this line's own.
REG = {0: '0', 1: 'k0', 2: 'k1', 3: 'fog', 4: 'col0', 5: 'col1', 8: 'tex0', 9: 'tex1',
       10: 'tex2', 11: 'tex3', 12: 'r0', 13: 'r1', 14: 'col1+r0', 15: 'e*f'}
MAP = {0: '{}', 1: '1-{}', 2: '2*{}-1', 3: '1-2*{}', 4: '{}-0.5', 5: '0.5-{}',
       6: '{}s', 7: '-{}s'}
OUT_SCALE = {0: '', 1: ' -0.5', 2: ' *2', 3: ' -0.5 *2', 4: ' *4', 6: ' /2'}


def u32(b, o=0):
    return struct.unpack_from('<I', b, o)[0]


def operand(byte, alpha):
    reg, chan, mp = byte & 0xF, byte & 0x10, (byte >> 5) & 7
    name = REG.get(reg, f'reg{reg}')
    if chan:
        name += '.a'
    elif alpha:
        name += '.b'
    # unsigned mappings clamp at 0 first (psh_regs.h comments)
    return MAP[mp].format(name)


def stage_inputs(word, alpha):
    return {k: operand((word >> s) & 0xFF, alpha)
            for k, s in (('a', 24), ('b', 16), ('c', 8), ('d', 0))}


def stage_output(word, alpha):
    cd, ab, sm = word & 0xF, (word >> 4) & 0xF, (word >> 8) & 0xF
    fl = word >> 12
    out = {}
    if ab:
        out['ab'] = (REG[ab], 'a.b' if fl & 2 and not alpha else 'a*b')
    if cd:
        out['cd'] = (REG[cd], 'c.d' if fl & 1 and not alpha else 'c*d')
    if sm:
        out['sum'] = (REG[sm], 'mux(ab, cd)' if fl & 4 else 'ab+cd')
    out['scale'] = OUT_SCALE.get((fl >> 3) & 7, f'?{(fl >> 3) & 7}')
    if not alpha and fl & 0x80:
        out['ab_blue_to_alpha'] = True
    if not alpha and fl & 0x40:
        out['cd_blue_to_alpha'] = True
    return out


def decode_block(w):
    ctl = w[53]
    n = ctl & 0xFF
    stages = []
    for i in range(n):
        stages.append({
            'color_in': stage_inputs(w[34 + i], False),
            'color_out': stage_output(w[45 + i], False),
            'alpha_in': stage_inputs(w[0 + i], True),
            'alpha_out': stage_output(w[26 + i], True),
            'k0': f'{w[10 + i]:#010x}', 'k1': f'{w[18 + i]:#010x}'})
    final = None
    if w[8] or w[9]:
        f0, f1 = stage_inputs(w[8], False), stage_inputs(w[9], False)
        final = {'a': f0['a'], 'b': f0['b'], 'c': f0['c'], 'd': f0['d'],
                 'e': f1['a'], 'f': f1['b'], 'g': operand((w[9] >> 8) & 0xFF, True),
                 'flags': f'{w[9] & 0xFF:#04x}'}
    # 0x800410B0: nibble i of word 57 (58) names the constant that goes to
    # FACTOR0[i] (FACTOR1[i]); nibbles 0 and 1 of word 59 the two final
    # combiner constants.
    kmap = {'factor0': [(w[57] >> (4 * i)) & 0xF for i in range(8)],
            'factor1': [(w[58] >> (4 * i)) & 0xF for i in range(8)],
            'final': [(w[59] >> (4 * i)) & 0xF for i in range(2)]}
    return {'stages': n, 'control_flags': f'{ctl >> 8:#x}', 'stage': stages, 'final': final,
            'constant_map': kmap,
            'texture_modes_word54': f'{w[54]:#x}',
            'texture_modes': [(w[54] >> (5 * i)) & 0x1F for i in range(4)],
            'dot_mapping': f'{w[55]:#x}', 'other_stage_input': f'{w[56]:#x}'}


def block_methods(w):
    out = []
    for first, m, cnt in BLOCK_METHODS:
        if m == 0x288 and not (w[8] or w[9]):
            continue
        for k in range(cnt):
            out.append([f'{m + 4 * k:#06x}', f'{w[first + k]:#010x}'])
    return out


def main():
    from nv2a_vsh import disassemble
    img = open(os.path.join(ROOT, 'out', 'rom', 'kernel.img'), 'rb').read()

    def rd(a, n):
        return img[a - BASE:a - BASE + n]

    progs, text = [], []
    for owner, field, addr, decl, where in PROGRAMS:
        tag, n = struct.unpack('<HH', rd(addr, 4))
        ins = [list(struct.unpack('<4I', rd(addr + 4 + 16 * i, 16))) for i in range(n)]
        dis = disassemble.disassemble(ins, explain=False)
        progs.append({'owner': owner, 'field': f'{field:#x}', 'addr': f'{addr:#010x}',
                      'tag': f'{tag:#06x}', 'count': n,
                      'end': f'{addr + 4 + 16 * n:#010x}',
                      'words': [[f'{x:#010x}' for x in i] for i in ins],
                      'attributes': [f'{x:#010x}' for x in decl], 'attributes_from': where,
                      'disassembly': dis})
        text.append(f'; {owner}+{field:#x}  {addr:#010x}  {n} instructions')
        text += [f'  {line}' for line in dis]
        text.append('')
    combs = []
    for owner, field, addr in COMBINERS:
        tag = u32(rd(addr, 4))
        w = list(struct.unpack('<60I', rd(addr + 4, 0xF0)))
        combs.append({'owner': owner, 'field': f'{field:#x}', 'addr': f'{addr:#010x}',
                      'tag': f'{tag:#010x}', 'words': [f'{x:#010x}' for x in w],
                      'methods': block_methods(w), 'decoded': decode_block(w)})
    a, n = RENDER_STATES
    rs = [{'index': f'{i:#04x}', 'header': f'{h:#010x}', 'method': f'{h & 0x1FFC:#06x}',
           'count': (h >> 18) & 0x7FF}
          for i, h in enumerate(struct.unpack(f'<{n}I', rd(a, 4 * n)))]
    data = {'source': 'out/rom/kernel.img', 'tool': 'nv2a_vsh 0.1.12 (.tools/pylib)',
            'programs': progs, 'combiners': combs,
            'render_states': {'addr': f'{a:#010x}', 'entries': rs}}
    out = os.path.join(ROOT, 'data', 'gpu')
    os.makedirs(out, exist_ok=True)
    files = {'gpu.json': (json.dumps(data, indent=1) + '\n').encode(),
             'programs.txt': ('\n'.join(text) + '\n').encode()}
    for name, b in files.items():
        with open(os.path.join(out, name), 'wb') as f:
            f.write(b)
    with open(os.path.join(out, 'SHA256SUMS'), 'w', newline='\n') as f:
        for name in sorted(files):
            f.write('%s  %s\n' % (hashlib.sha256(files[name]).hexdigest(), name))
    print(f'{len(progs)} programs, {len(combs)} combiner blocks -> {out}')


if __name__ == '__main__':
    main()
