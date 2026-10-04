#!/usr/bin/env python3
"""Compiler launcher for ps2/src units on Windows (MinGW/COFF).
COFF has no ld -r + objcopy --weaken and clang ignores -fsemantic-interposition there, so a plain build would let the
compiler fold calls to functions that port/src replaces. This emits unoptimised IR, marks every externally visible
definition weak, then runs the normal optimiser: calls stay real calls and port/src's strong definitions win at link time.
Usage (CMake RULE_LAUNCH_COMPILE):  weakcc.py <clang++> <flags...> -o out.obj -c src.cpp"""
import os, re, subprocess, sys

SKIP = r'(internal|private|linkonce\w*|weak\w*|common|available_externally|extern_weak|appending)\b'
GVAR = re.compile(r'((dso_local|hidden|protected|local_unnamed_addr|unnamed_addr|thread_local\([a-z]+\)) )*(global|constant) ')

def weaken(line):
    if line.startswith('define '):
        if not re.match(r'define\s+' + SKIP, line):
            return line.replace('define ', 'define weak ', 1)
    elif line.startswith('@') and ' = ' in line:
        rest = line.split(' = ', 1)[1]
        if not re.match(SKIP, rest) and GVAR.match(rest):
            return line.replace(' = ', ' = weak ', 1)
    return line

STRG = re.compile(r'^@\.str(\.\d+)? = private .*?(, align \d+)$')

def move_str(line):
    # writable string literals (-fwritable-strings) would otherwise land inside .data between game tables that the game
    # indexes past their ends (ItemPutListTbl12_bytes); park them in a section sorted to the end of .data
    m = STRG.match(line)
    if m and ' global ' in line:
        return line[:m.start(2)] + ', section ".data$zzstr"' + m.group(2)
    return line

def main():
    argv = sys.argv[1:]
    cc = argv[0]; args = argv[1:]
    if '-c' not in args or '-o' not in args:
        sys.exit(subprocess.call([cc] + args))
    out = args[args.index('-o') + 1]
    src = args[args.index('-c') + 1]
    ll = out + '.ll'
    # step 1: front end only (keeps every flag, including -MD/-MF dependency output)
    a1 = []
    skip = False
    for i, a in enumerate(args):
        if skip: skip = False; continue
        if a == '-c': a1 += ['-S', '-emit-llvm', '-Xclang', '-disable-llvm-passes']; continue
        if a == '-o': a1 += ['-o', ll]; skip = True; continue
        a1.append(a)
    r = subprocess.call([cc] + a1)
    if r: sys.exit(r)
    with open(ll, encoding='utf-8', errors='surrogateescape') as f:
        text = f.read()
    with open(ll, 'w', encoding='utf-8', errors='surrogateescape', newline='\n') as f:
        f.write('\n'.join(move_str(weaken(l)) for l in text.split('\n')))
    # step 2: optimiser + code generation from the weakened IR
    keep = [a for a in args if a.startswith(('--target', '-O', '-m', '-ffunction-sections', '-fdata-sections', '-fno-', '-fuse-ld')) and not a.startswith(('-fno-strict', '-fno-common-x'))]
    keep = [a for a in keep if a not in ('-fno-strict-return', '-fno-strict-aliasing')]
    r = subprocess.call([cc, '-Wno-override-module', '-c', ll, '-o', out] + [k for k in keep if k.startswith(('--target', '-O', '-m', '-ffunction-sections', '-fdata-sections'))])
    try: os.remove(ll)
    except OSError: pass
    sys.exit(r)

main()
