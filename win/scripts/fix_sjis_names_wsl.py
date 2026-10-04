# run inside WSL: python3 /mnt/e/Chronicle-project-Claude/fix_sjis_names_wsl.py
# Copies files whose names are raw Shift-JIS bytes into win-data using proper Unicode (CP932-decoded) names.
import os, shutil, sys
src = os.path.expanduser('~/Chronicle/data')
dst = '/mnt/e/Chronicle-project-Claude/win-data'
n = bad = 0
for dp, dns, fns in os.walk(os.fsencode(src)):
    rel_dir = os.path.relpath(dp, os.fsencode(src))
    for fn in fns:
        parts = [] if rel_dir == b'.' else rel_dir.split(b'/')
        full = b'/'.join(parts + [fn])
        try:
            full.decode('utf-8'); continue          # normal name: robocopy already did it
        except UnicodeDecodeError:
            pass
        try:
            u = [x.decode('cp932') for x in parts + [fn]]
        except UnicodeDecodeError:
            print('undecodable', full); bad += 1; continue
        out = os.path.join(dst, *u)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copyfile(os.path.join(dp, fn), out); n += 1
print('copied', n, 'bad', bad)
