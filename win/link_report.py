#!/usr/bin/env python3
"""Summarise lld's 'undefined symbol' warnings from win-build.log.
Expected: referenced only from replaced game bodies (section '.weak.<name>.default'). Anything else is a real missing symbol."""
import re, sys
raw = open(sys.argv[1], 'rb').read()
text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8', errors='replace')
lines = [l.rstrip('\r') for l in text.split('\n')]
expected, suspicious, indirect_d = {}, {}, {}
i = 0
while i < len(lines):
    m = re.match(r'ld\.lld: (?:warning|error): undefined symbol: (.+)', lines[i])
    if m:
        name, refs, j = m.group(1).strip(), [], i + 1
        while j < len(lines) and lines[j].startswith('>>>'):
            refs.append(lines[j]); j += 1
        live = [r for r in refs if 'referenced by' in r and '.weak.' not in r and '.refptr.' not in r]
        indirect = [r for r in refs if '.refptr.' in r]
        (suspicious if live else (indirect_d if indirect and len(indirect) == len(refs) else expected))[name] = live or refs
        i = j
    else:
        i += 1
print(f'undefined symbols: {len(expected)} only referenced from replaced (dead) bodies, {len(indirect_d)} via .refptr indirection (liveness unknown: run to find out), {len(suspicious)} suspicious')
for n in indirect_d: print('  indirect', n)
for n, r in list(suspicious.items())[:40]:
    print('  SUSPICIOUS', n); [print('    ', x) for x in r[:3]]
