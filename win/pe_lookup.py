#!/usr/bin/env python3
"""pe_lookup.py <exe> <rva> [<rva> ...] : section and nearest preceding COFF symbol for each RVA (MinGW exes keep the symbol table)."""
import struct, sys
data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec, = struct.unpack_from('<H', data, pe + 6)
symoff, nsym = struct.unpack_from('<II', data, pe + 12)
optsz, = struct.unpack_from('<H', data, pe + 20)
sec0 = pe + 24 + optsz
secs = []
for i in range(nsec):
    o = sec0 + 40 * i
    name = data[o:o+8].rstrip(b'\0').decode('latin1')
    vsize, va, rawsz, rawptr = struct.unpack_from('<IIII', data, o + 8)
    chars, = struct.unpack_from('<I', data, o + 36)
    secs.append((name, va, vsize, chars))
strtab = symoff + 18 * nsym
def symname(o):
    if data[o:o+4] == b'\0\0\0\0':
        off, = struct.unpack_from('<I', data, o + 4)
        e = data.index(b'\0', strtab + off)
        return data[strtab + off:e].decode('latin1')
    return data[o:o+8].rstrip(b'\0').decode('latin1')
syms = []
i = 0
while i < nsym:
    o = symoff + 18 * i
    value, secnum = struct.unpack_from('<Ih', data, o + 8)
    aux = data[o + 17]
    if secnum > 0:
        name, va, vsize, _ = secs[secnum - 1]
        syms.append((va + value, symname(o), name))
    i += 1 + aux
syms.sort()
import bisect
keys = [s[0] for s in syms]
for r in sys.argv[2:]:
    rva = int(r, 16)
    sec = next((f'{n} [{"W" if c & 0x80000000 else "-"}{"X" if c & 0x20000000 else "-"}{"R" if c & 0x40000000 else "-"}]' for n, va, vs, c in secs if va <= rva < va + vs), '?')
    k = bisect.bisect_right(keys, rva) - 1
    near = syms[k] if k >= 0 else None
    print(f'RVA {rva:X}  section {sec}  nearest symbol: ' + (f'{near[1]} +0x{rva - near[0]:X}' if near else 'none'))
