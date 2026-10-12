"""Helpers for the NV2A command stream and textures (C8).

- Method names from xemu's nv2a_regs.h (ref/xemu/, optional; without it the
  names are the hex method numbers).
- decode_pb: pushbuffer words -> (method, value) pairs. Command forms after
  xemu hw/xbox/nv2a/pfifo.c (pfifo_run_pusher): increasing (word &
  0xe0030003 == 0), non-increasing (== 0x40000000), old jump (word &
  0xe0000003 == 0x20000000), jump ((word & 3) == 1), call ((word & 3) == 2),
  return (0x00020000). Count bits 18-28, subchannel 13-15, method 0-12.
- read_trace: the nv2a_pgraph_method* lines of a trace as (method, value);
  xemu writes a run of ARRAY_ELEMENT16 as one line '* n' with n = words - 1
  (tools/xemu/README.md, "Limits"). A method xemu does not handle comes
  twice, as nv2a_pgraph_method and nv2a_pgraph_method_unhandled
  (pgraph.c, label unhandled); the second line is skipped.
- texture_length: bytes of a swizzled texture from its format word, after
  pgraph_get_texture_length and kelvin_color_format_info_map in xemu
  hw/xbox/nv2a/pgraph/texture.c; field masks from NV_PGRAPH_TEXFMT0_* and
  NV2A_CUBEMAP_FACE_ALIGNMENT (128) in nv2a_regs.h.
"""

import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
REGS = os.path.join(ROOT, 'ref', 'xemu', 'hw_xbox_nv2a_nv2a_regs.h')

ARRAY_ELEMENT16 = 0x1800
BEGIN_END = 0x17FC


def method_names():
    names = {}
    if os.path.exists(REGS):
        for line in open(REGS):
            m = re.match(r'#\s*define (NV097_\w+)\s+0[xX]([0-9A-Fa-f]+)\s*$', line)
            if m:
                v = int(m.group(2), 16)
                if v < 0x2000 and v % 4 == 0 and v not in names:
                    names[v] = m.group(1)[6:]
    return names


_NAMES = None


def mname(m):
    """Name of a method; an array element as BASE+offset."""
    global _NAMES
    if _NAMES is None:
        _NAMES = method_names()
    if m in _NAMES:
        return _NAMES[m]
    base = max((k for k in _NAMES if k <= m), default=None)
    if base is None or m - base > 0x80:
        return f'{m:#06x}'
    return f'{_NAMES[base]}+{m - base:#x}'


class PbError(Exception):
    pass


def decode_pb(words, start=0):
    """(method, value) pairs of a list of words that starts on a command.
    Jumps, calls and returns are given back as ('jump', target) etc.; the
    caller follows them. Raises PbError on an unfinished command."""
    out = []
    i = 0
    n = len(words)
    while i < n:
        w = words[i]
        i += 1
        if (w & 0xE0000003) == 0x20000000:
            out.append(('oldjump', w & 0x1FFFFFFF))
        elif (w & 3) == 1:
            out.append(('jump', w & 0xFFFFFFFC))
        elif (w & 3) == 2:
            out.append(('call', w & 0xFFFFFFFC))
        elif w == 0x00020000:
            out.append(('return', 0))
        elif (w & 0xE0030003) in (0, 0x40000000):
            inc = (w & 0xE0030003) == 0
            m, cnt, sub = w & 0x1FFC, (w >> 18) & 0x7FF, (w >> 13) & 7
            if i + cnt > n:
                raise PbError(f'command at word {i - 1} needs {cnt} words, {n - i} left')
            for k in range(cnt):
                out.append((m + 4 * k if inc else m, words[i + k], sub))
            i += cnt
        else:
            raise PbError(f'reserved command {w:#x} at word {i - 1}')
    return out


TRACE_RE = re.compile(r'nv2a_pgraph_method(_unhandled|_abbrev)? (\d+): 0x([0-9a-f]+) -> '
                      r'0x([0-9a-f]+) (?:(\S+) )?(?:\* (\d+)|(0x[0-9a-f]+))')


def read_trace(data):
    """Method lines of a trace (bytes or str) as dicts with the byte offset."""
    if isinstance(data, bytes):
        data = data.decode('latin-1')
    out = []
    pos = 0
    for line in data.splitlines(True):
        if line.startswith('nv2a_pgraph_method'):
            m = TRACE_RE.match(line)
            if m and m.group(1) != '_unhandled':
                meth = int(m.group(4), 16)
                if m.group(6) is not None:
                    out.append({'pos': pos, 'm': meth, 'ae16_words': int(m.group(6)) + 1})
                else:
                    out.append({'pos': pos, 'm': meth, 'v': int(m.group(7), 16),
                                'sub': int(m.group(2))})
        pos += len(line)
    return out


# bytes per pixel and linear flag, xemu kelvin_color_format_info_map
COLOR_FORMATS = {
    0x00: (1, False), 0x01: (1, False), 0x02: (2, False), 0x03: (2, False),
    0x04: (2, False), 0x05: (2, False), 0x06: (4, False), 0x07: (4, False),
    0x0B: (1, False), 0x10: (2, True), 0x11: (2, True), 0x12: (4, True),
    0x13: (1, True), 0x17: (2, True), 0x19: (1, False), 0x1A: (2, False),
    0x1B: (1, True), 0x1C: (2, True), 0x1D: (2, True), 0x1E: (4, True),
    0x1F: (1, True), 0x20: (2, True), 0x27: (2, False), 0x28: (2, False),
    0x29: (2, False), 0x2C: (2, False), 0x2E: (4, True), 0x2F: (4, True),
    0x30: (2, True), 0x31: (2, True), 0x35: (2, True), 0x3A: (4, False),
    0x3B: (4, False), 0x3C: (4, False), 0x3F: (4, True), 0x40: (4, True),
    0x41: (4, True),
}


def texture_shape(fmt):
    return {'cubemap': bool(fmt & 4), 'dims': (fmt >> 4) & 0xF,
            'color': (fmt >> 8) & 0xFF, 'levels': (fmt >> 16) & 0xF,
            'log_u': (fmt >> 20) & 0xF, 'log_v': (fmt >> 24) & 0xF,
            'log_p': (fmt >> 28) & 0xF}


def texture_length(fmt, pitch=0, height=0):
    s = texture_shape(fmt)
    bpp, linear = COLOR_FORMATS[s['color']]
    if linear:
        return height * pitch
    w, h = 1 << s['log_u'], 1 << s['log_v']
    n = 0
    for _ in range(max(s['levels'], 1)):
        n += max(w, 1) * max(h, 1) * bpp
        w, h = w // 2, h // 2
    if s['cubemap']:
        n = (n + 127) & ~127
        n *= 6
    if s['dims'] >= 3:
        n *= 1 << s['log_p']
    return n
