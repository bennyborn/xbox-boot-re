"""Minimal client for QEMU's GDB remote stub (i386 target).

Only what the bench needs: stop, continue, hardware breakpoints, the
general registers and memory reads at virtual addresses.
"""

import socket
import struct
import time

# Order of the 'g' packet for i386 in QEMU (gdb-xml/i386-32bit.xml):
# eax ecx edx ebx esp ebp esi edi eip eflags cs ss ds es fs gs
REGS = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi',
        'eip', 'eflags', 'cs', 'ss', 'ds', 'es', 'fs', 'gs']


class GDB:
    def __init__(self, port, timeout=30.0):
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.s = socket.create_connection(('127.0.0.1', port), 2)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.1)
        self.s.settimeout(None)
        self.buf = b''

    def _recv(self):
        while True:
            i = self.buf.find(b'$')
            if i >= 0:
                j = self.buf.find(b'#', i)
                if j >= 0 and len(self.buf) >= j + 3:
                    body = self.buf[i + 1:j]
                    self.buf = self.buf[j + 3:]
                    self.s.sendall(b'+')
                    return body.decode('latin-1')
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError('gdb stub closed')
            self.buf += chunk

    def _send(self, body):
        data = body.encode('latin-1')
        pkt = b'$' + data + b'#%02x' % (sum(data) & 0xFF)
        self.s.sendall(pkt)

    def req(self, body):
        self._send(body)
        while True:
            r = self._recv()
            if r.startswith('O') and r != 'OK':
                continue    # console output
            return r

    def interrupt(self):
        self.s.sendall(b'\x03')
        return self._recv()

    def cont(self):
        self._send('c')

    def wait_stop(self, timeout=None):
        """The stop reply, or None if the guest has not stopped within timeout
        seconds; then it is stopped with an interrupt."""
        if timeout is None:
            return self._recv()
        self.s.settimeout(timeout)
        try:
            return self._recv()
        except socket.timeout:
            pass
        finally:
            self.s.settimeout(None)
        self.interrupt()
        return None

    def hbreak(self, addr, on=True):
        r = self.req(f"{'Z' if on else 'z'}1,{addr:x},1")
        if r != 'OK':
            raise RuntimeError(f'hbreak {addr:#x}: {r!r}')

    def regs(self):
        r = self.req('g')
        vals = struct.unpack_from('<16I', bytes.fromhex(r[:16 * 8]))
        return dict(zip(REGS, vals))

    def read(self, addr, size):
        out = b''
        while size:
            n = min(size, 0x800)
            r = self.req(f'm{addr:x},{n:x}')
            if r.startswith('E'):
                raise RuntimeError(f'read {addr:#x}: {r}')
            out += bytes.fromhex(r)
            addr += n
            size -= n
        return out

    def write(self, addr, data):
        r = self.req(f'M{addr:x},{len(data):x}:{data.hex()}')
        if r != 'OK':
            raise RuntimeError(f'write {addr:#x}: {r!r}')

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass
