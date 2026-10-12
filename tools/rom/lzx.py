"""LZX decoder for the kernel image in the BIOS.

Written from the public specification [MS-PATCH] "LZX DELTA Compression and
Decompression", section 2 (see docs/provenance.md). The framing around it
(one 16-bit compressed size and one 16-bit uncompressed size per 32 KiB
chunk) and the window size of 0x20000 come from the 2BL itself, see
docs/decomp/Rom.md.

Where this decoder departs from the spec, checked against the BIOS:
- No reference data, and no Extra Length field ([MS-PATCH] 2.6.6): a length
  of 257 is final. The kernel has 937 such matches and decodes to the exact
  size only this way, so the field does not exist in this variant.
- Aligned offset bits are used from 3 footer bits on, as [MS-PATCH] 2.6.3
  says (formatted offset > 15), not from 4 on as its pseudocode in 2.7 says.
  With the pseudocode, the first aligned block of the kernel fails.
A match length of 257 is counted in `long_matches` for the report.
"""

VERBATIM, ALIGNED, UNCOMPRESSED = 1, 2, 3
MAIN_LITERALS = 256
LENGTH_SYMBOLS = 249
PRETREE_SYMBOLS = 20
ALIGNED_SYMBOLS = 8
MAX_CODE_BITS = 16
CHUNK = 0x8000

# [MS-PATCH] 2.1.6: number of position slots per window size.
POSITION_SLOTS = {17: 34, 18: 36, 19: 38, 20: 42, 21: 50, 22: 66, 23: 98, 24: 162, 25: 290}


def footer_bits_and_bases(slots):
    """[MS-PATCH] 2.6.2: footer bits and base position of every slot."""
    bits, bases, base = [], [], 0
    for slot in range(slots):
        n = 0 if slot < 4 else min((slot - 2) // 2, 17)
        bits.append(n)
        bases.append(base)
        base += 1 << n
    return bits, bases


class LzxError(Exception):
    pass


class BitReader:
    """[MS-PATCH] 2.1.1: little-endian 16-bit words, read MSB first."""

    def __init__(self, data):
        self.data = data
        self.pos = 0  # next byte to load into the buffer
        self.buf = 0
        self.count = 0

    def _fill(self, n):
        while self.count < n:
            if self.pos + 1 < len(self.data):
                word = self.data[self.pos] | self.data[self.pos + 1] << 8
            else:
                word = 0  # past the end of the chunk, as padding
            self.pos += 2
            self.buf = (self.buf << 16 | word) & 0xFFFFFFFFFFFF
            self.count += 16

    def peek(self, n):
        self._fill(n)
        return (self.buf >> (self.count - n)) & ((1 << n) - 1)

    def skip(self, n):
        self._fill(n)
        self.count -= n

    def bits(self, n):
        if n == 0:
            return 0
        v = self.peek(n)
        self.count -= n
        return v

    def align_to_bytes(self):
        """[MS-PATCH] 2.3.2.1: 1 to 16 bits of padding, then a byte stream."""
        pad = self.count % 16
        self.skip(pad if pad else 16)
        # Whole words still buffered were loaded but not consumed.
        self.pos -= self.count // 8
        self.buf = 0
        self.count = 0

    def raw_bytes(self, n):
        if self.pos + n > len(self.data):
            raise LzxError("uncompressed block runs past the chunk")
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b


class Huffman:
    """Canonical code from path lengths ([MS-PATCH] 2.4), table lookup."""

    def __init__(self, lengths):
        self.table = None
        used = sorted((l, s) for s, l in enumerate(lengths) if l)
        if not used:
            return
        table = [None] * (1 << MAX_CODE_BITS)
        code, prev_len = 0, used[0][0]
        for l, s in used:
            code <<= l - prev_len
            prev_len = l
            lo = code << (MAX_CODE_BITS - l)
            hi = (code + 1) << (MAX_CODE_BITS - l)
            if hi > len(table):
                raise LzxError("over-subscribed Huffman code")
            table[lo:hi] = [(s, l)] * (hi - lo)
            code += 1
        self.table = table

    def decode(self, r):
        if self.table is None:
            raise LzxError("decode from an empty tree")
        entry = self.table[r.peek(MAX_CODE_BITS)]
        if entry is None:
            raise LzxError("invalid Huffman code")
        r.skip(entry[1])
        return entry[0]


class Decoder:
    def __init__(self, window_bits):
        slots = POSITION_SLOTS[window_bits]
        self.window_size = 1 << window_bits
        self.footer_bits, self.base_position = footer_bits_and_bases(slots)
        self.main_lengths = [0] * (MAIN_LITERALS + 8 * slots)
        self.length_lengths = [0] * LENGTH_SYMBOLS
        self.window = bytearray()  # whole output so far, untranslated
        self.r = [1, 1, 1]  # [MS-PATCH] 2.1.4: initial R0, R1, R2
        self.started = False
        self.e8_size = None
        self.block_type = None
        self.block_left = 0
        self.chunks = 0
        self.long_matches = 0

    # -- trees ------------------------------------------------------------

    def _read_lengths(self, r, lengths, first, last):
        """[MS-PATCH] 2.5: pretree, then delta-coded path lengths."""
        pre = Huffman([r.bits(4) for _ in range(PRETREE_SYMBOLS)])
        x = first
        while x < last:
            code = pre.decode(r)
            if code <= 16:
                lengths[x] = (lengths[x] - code + 17) % 17
                x += 1
            elif code == 17:
                n = r.bits(4) + 4
                lengths[x:x + n] = [0] * n
                x += n
            elif code == 18:
                n = r.bits(5) + 20
                lengths[x:x + n] = [0] * n
                x += n
            else:
                n = r.bits(1) + 4
                code = pre.decode(r)
                if code > 16:
                    raise LzxError("run code inside a run")
                value = (lengths[x] - code + 17) % 17
                lengths[x:x + n] = [value] * n
                x += n
        if x != last:
            raise LzxError("path length run past the end of the tree")

    def _read_block_header(self, r):
        self.block_type = r.bits(3)
        self.block_left = r.bits(24)
        if self.block_type == ALIGNED:
            self.aligned = Huffman([r.bits(3) for _ in range(ALIGNED_SYMBOLS)])
        if self.block_type in (VERBATIM, ALIGNED):
            self._read_lengths(r, self.main_lengths, 0, MAIN_LITERALS)
            self._read_lengths(r, self.main_lengths, MAIN_LITERALS, len(self.main_lengths))
            self._read_lengths(r, self.length_lengths, 0, LENGTH_SYMBOLS)
            self.main = Huffman(self.main_lengths)
            self.length = Huffman(self.length_lengths)
        elif self.block_type == UNCOMPRESSED:
            r.align_to_bytes()
            raw = r.raw_bytes(12)
            self.r = [int.from_bytes(raw[i:i + 4], "little") for i in (0, 4, 8)]
        else:
            raise LzxError("invalid block type %d" % self.block_type)

    # -- tokens -----------------------------------------------------------

    def _match(self, r, element):
        """[MS-PATCH] 2.7 for one main tree element >= 256."""
        header = (element - MAIN_LITERALS) & 7
        length = header + 2
        if header == 7:
            length = self.length.decode(r) + 7 + 2
        slot = (element - MAIN_LITERALS) >> 3
        R = self.r
        if slot == 0:
            offset = R[0]
        elif slot == 1:
            offset = R[1]
            R[0], R[1] = R[1], R[0]
        elif slot == 2:
            offset = R[2]
            R[0], R[2] = R[2], R[0]
        else:
            n = self.footer_bits[slot]
            if self.block_type == ALIGNED and n >= 3:  # 2.6.3, see module doc
                verbatim = r.bits(n - 3) << 3
                aligned = self.aligned.decode(r)
            else:
                verbatim, aligned = r.bits(n), 0
            offset = self.base_position[slot] + verbatim + aligned - 2
            R[2], R[1], R[0] = R[1], R[0], offset
        if length == 257:
            self.long_matches += 1
        return length, offset

    def _tokens(self, r, n):
        w = self.window
        end = len(w) + n
        while len(w) < end:
            element = self.main.decode(r)
            if element < MAIN_LITERALS:
                w.append(element)
                continue
            length, offset = self._match(r, element)
            if offset > len(w) or offset >= self.window_size:
                raise LzxError("match reaches before the start of the output")
            if len(w) + length > end:
                raise LzxError("match runs past the block or chunk")
            start = len(w) - offset
            if offset >= length:
                w += w[start:start + length]
            else:
                for i in range(length):
                    w.append(w[start + i])

    # -- chunks -----------------------------------------------------------

    def chunk(self, data, out_size):
        """Decode one chunk; returns its output after E8 reversal."""
        r = BitReader(data)
        if not self.started:
            self.started = True
            if r.bits(1):
                self.e8_size = r.bits(16) << 16 | r.bits(16)
        begin = len(self.window)
        left = out_size
        while left:
            if self.block_left == 0:
                self._read_block_header(r)
                self.block_size = self.block_left
            n = min(self.block_left, left)
            if self.block_type == UNCOMPRESSED:
                self.window += r.raw_bytes(n)
            else:
                self._tokens(r, n)
            self.block_left -= n
            left -= n
            if self.block_type == UNCOMPRESSED and self.block_left == 0:
                if self.block_size % 2:
                    r.raw_bytes(1)  # [MS-PATCH] 2.3.2.1: realign
                r = self._resume_bits(r)
        out = bytearray(self.window[begin:])
        if self.e8_size is not None and self.chunks < 0x8000:
            self._undo_e8(out, begin)
        self.chunks += 1
        return bytes(out)

    @staticmethod
    def _resume_bits(r):
        """After an uncompressed block the bitstream resumes at r.pos."""
        nr = BitReader(r.data)
        nr.pos = r.pos
        return nr

    def _undo_e8(self, b, chunk_offset):
        """[MS-PATCH] 2.2.2, reversal after each chunk."""
        if len(b) <= 10:
            return
        i, stop = 0, len(b) - 10
        while i < stop:
            if b[i] == 0xE8:
                cur = chunk_offset + i
                value = int.from_bytes(b[i + 1:i + 5], "little", signed=True)
                if -cur <= value < self.e8_size:
                    disp = value - cur if value >= 0 else value + self.e8_size
                    b[i + 1:i + 5] = (disp & 0xFFFFFFFF).to_bytes(4, "little")
                i += 4
            i += 1


def decompress_chunks(stream, window_bits=17):
    """Decode a sequence of (u16 packed, u16 unpacked, packed bytes) chunks
    until the stream is used up, as the 2BL does.

    Returns (output, decoder) so callers can inspect e8_size, chunks and
    long_matches.
    """
    d = Decoder(window_bits)
    out = bytearray()
    pos = 0
    while pos < len(stream):
        if pos + 4 > len(stream):
            raise LzxError("truncated chunk header")
        packed = stream[pos] | stream[pos + 1] << 8
        unpacked = stream[pos + 2] | stream[pos + 3] << 8
        out += d.chunk(stream[pos + 4:pos + 4 + packed], unpacked)
        pos += 4 + packed
    return bytes(out), d
