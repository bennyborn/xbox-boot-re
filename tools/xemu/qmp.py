"""Minimal QMP client (QEMU Machine Protocol, JSON lines over TCP)."""

import json
import socket
import time


class QMPError(RuntimeError):
    pass


class QMP:
    def __init__(self, port, timeout=30.0):
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.sock = socket.create_connection(('127.0.0.1', port), 2)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.1)
        self.sock.settimeout(None)
        self.f = self.sock.makefile('rw', encoding='utf-8', newline='\n')
        self.events = []
        self.greeting = json.loads(self.f.readline())
        self.cmd('qmp_capabilities')

    def _read(self):
        while True:
            line = self.f.readline()
            if not line:
                raise QMPError('QMP connection closed')
            m = json.loads(line)
            if 'event' in m:
                self.events.append(m)
                continue
            return m

    def cmd(self, _command, **args):
        msg = {"execute": _command}
        if args:
            msg['arguments'] = args
        self.f.write(json.dumps(msg) + '\n')
        self.f.flush()
        r = self._read()
        if 'error' in r:
            raise QMPError(f"{_command}: {r['error'].get('desc')}")
        return r.get('return')

    def hmp(self, line):
        return self.cmd('human-monitor-command', **{'command-line': line})

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass
