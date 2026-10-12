"""Derives the constants of core/src/x87.c and checks them against the file.

    python core/test/gen_trig.py

pi comes from Machin's formula, pi = 16 atan(1/5) - 4 atan(1/239), summed
in decimal arithmetic with 80 digits; nothing is copied from elsewhere.
pi/2 is split into three doubles for the argument reduction: hi and mid
keep 33 significant bits each, so n * hi and n * mid are exact for
|n| < 2^20; lo is the rest, rounded to double. 2/pi is the nearest double.
The Taylor coefficients 1/k! are written as quotients in x87.c and need no
table here.
"""

import math
import os
import re
import sys
from decimal import Decimal, getcontext

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
getcontext().prec = 80


def atan_inv(n):
    """atan(1/n) as a decimal series."""
    x = Decimal(1) / n
    total, power, k = Decimal(0), x, 0
    while True:
        term = power / (2 * k + 1)
        if term < Decimal(10) ** -78:
            return total
        total += -term if k % 2 else term
        power /= n * n
        k += 1


def top_bits(d, bits):
    """The double nearest to d, cut to its `bits` leading significant bits."""
    m, e = math.frexp(float(d))
    return math.ldexp(math.floor(m * 2 ** bits) / 2 ** bits, e)


def constants():
    pi = 16 * atan_inv(5) - 4 * atan_inv(239)
    pio2 = pi / 2
    hi = top_bits(pio2, 33)
    mid = top_bits(pio2 - Decimal(hi), 33)
    lo = float(pio2 - Decimal(hi) - Decimal(mid))
    return pi, {'PIO2_HI': hi, 'PIO2_MID': mid, 'PIO2_LO': lo, 'INV_PIO2': float(2 / pi)}


def main():
    pi, want = constants()
    print('pi =', str(pi)[:60])
    src = open(os.path.join(ROOT, 'core', 'src', 'x87.c'), encoding='utf-8').read()
    bad = 0
    for name, value in want.items():
        m = re.search(r'#define\s+' + name + r'\s+(\S+)', src)
        got = float.fromhex(m.group(1)) if m else None
        ok = got == value
        bad += not ok
        print(f'{"ok  " if ok else "FAIL"}  {name:8} {value.hex():24} in x87.c: {m.group(1) if m else "-"}')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
