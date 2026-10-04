#!/usr/bin/env python3
"""Generate the Windows source tree Chronicle-win-src/ from the pristine upstream copy Chronicle-src/.
 - copies ps2/src, ps2/include, port/include, port/src (tests excluded)
 - rewrites C/C++ `long` (and `unsigned long`, `long int`) to 64-bit `long long` outside strings/comments, in game code
   and replacement units (not platform/gfx/audio): the PS2 and Linux x64 have 64-bit long, Windows has 32-bit
 - swaps in the Windows arena memory unit, writes the Windows CMakeLists.txt and the compiler launcher
Upstream files in Chronicle-src/ are never modified."""
import re, shutil, sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
DST = SRC.parent / 'Chronicle-win-src'
WIN = SRC / 'win'

CODE_EXT = {'.cpp', '.c', '.h', '.hpp', '.hh', '.inc'}
HOST_DIRS = ('port/src/platform/', 'port/src/gfx/', 'port/src/audio/')
NONCODE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
LONG = re.compile(r'\b(?:(unsigned|signed)\s+)?long\b(?:\s+(long|int|double)\b)?')

def long_sub(m):
    if m.group(2) in ('long', 'double'):
        return m.group(0)
    return (m.group(1) + ' ' if m.group(1) else '') + 'long long'

def rewrite_long(text):
    out, last, n = [], 0, 0
    def code(chunk):
        nonlocal n
        new, k = LONG.subn(long_sub, chunk)
        n += sum(1 for m in LONG.finditer(chunk) if m.group(2) not in ('long', 'double'))
        return new
    for m in NONCODE.finditer(text):
        out.append(code(text[last:m.start()])); out.append(m.group(0)); last = m.end()
    out.append(code(text[last:]))
    return ''.join(out), n


# Windows source patches: (file, old, new). Each `old` must occur; a mismatch means upstream changed and the patch needs review.
PATCHES = [
    ('port/src/runtime.cpp', '#ifdef __APPLE__\n#define LIBC_NOEXCEPT', '#if defined(__APPLE__) || defined(_WIN32)\n#define LIBC_NOEXCEPT'),
    ('port/src/sce/sifdev.cpp', '#include <fcntl.h>',
     '#include <fcntl.h>\n#include <io.h>\n#ifndef O_CLOEXEC\n#define O_CLOEXEC 0\n#endif\n#ifndef O_BINARY\n#define O_BINARY 0\n#endif'),
    ('port/src/sce/sifdev.cpp', 'return host | O_CLOEXEC;', 'return host | O_CLOEXEC | O_BINARY;'),
    ('port/src/sce/sifdev.cpp', '::open(path.c_str(), HostFlags(flags), 0644)', '::_wopen(path.c_str(), HostFlags(flags), 0644)'),
    ('port/src/sce/libmc.cpp', 'gmtime_r(&seconds, &utc);', 'gmtime_s(&utc, &seconds);'),
    ('port/src/sce/libmc.cpp', 'std::fopen(path.c_str(), (flag & kOpenWrite) != 0 ? "r+b" : "rb")', '_wfopen(path.c_str(), (flag & kOpenWrite) != 0 ? L"r+b" : L"rb")'),
    ('port/src/sce/libmc.cpp', 'std::fopen(path.c_str(), "w+b")', '_wfopen(path.c_str(), L"w+b")'),
    ('port/src/sce/libmc.cpp', 'McDateTime DateTime(const fs::path &path) {',
     'static std::string PathUtf8(const fs::path &p) { auto u = p.u8string(); return std::string(u.begin(), u.end()); }\nMcDateTime DateTime(const fs::path &path) {'),
    ('port/src/sce/libmc.cpp', 'Matches(pattern.c_str(), entry.path().filename().c_str())', 'Matches(pattern.c_str(), PathUtf8(entry.path().filename()).c_str())'),
    ('port/src/sce/libmc.cpp', '[](const fs::directory_entry &entry) { return entry.path().filename().string(); }', '[](const fs::directory_entry &entry) { return PathUtf8(entry.path().filename()); }'),
    ('port/src/sce/libmc.cpp', 'Entry(entry.path(), entry.path().filename().string(), entry.is_directory(error))', 'Entry(entry.path(), PathUtf8(entry.path().filename()), entry.is_directory(error))'),
    ('port/src/dataread.cpp', 'std::string relative = it->path().lexically_relative(root).generic_string();',
     'std::string relative = RelativeAsGameName(it->path().lexically_relative(root));'),
    ('port/src/dataread.cpp', 'std::unordered_map<std::string, fs::path> data_index;',
     'extern "C" __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned, unsigned long, const wchar_t *, int, char *, int, const char *, int *);\n'
     '// Windows stores the Shift-JIS file names as Unicode; the game asks for the original CP932 bytes.\n'
     'static std::string RelativeAsGameName(const fs::path &p) {\n'
     '    std::wstring w = p.generic_wstring();\n'
     '    int n = WideCharToMultiByte(932, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);\n'
     '    std::string out(n > 0 ? n : 0, 0);\n'
     '    if (n > 0) WideCharToMultiByte(932, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);\n'
     '    return out;\n}\n'
     'std::unordered_map<std::string, fs::path> data_index;'),
    ('tools/dcdata/dcdata.hpp', 'inline Summary Extract(',
     'extern "C" __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned, unsigned long, const char *, int, wchar_t *, int);\n'
     'inline fs::path Cp932Path(const std::string &s) { // game names are Shift-JIS bytes\n'
     '    int n = MultiByteToWideChar(932, 0, s.data(), (int)s.size(), nullptr, 0);\n'
     '    std::wstring w(n > 0 ? n : 0, 0);\n'
     '    if (n > 0) MultiByteToWideChar(932, 0, s.data(), (int)s.size(), w.data(), n);\n'
     '    return fs::path(w);\n}\n'
     'inline Summary Extract('),
    ('tools/dcdata/dcdata.hpp', 'fs::path target = out / fs::path(record->path);', 'fs::path target = out / Cp932Path(record->path);'),
    ('tools/dcdata/dcdata.hpp', 'IsCurrent(root / fs::path(record->path), record->size)', 'IsCurrent(root / Cp932Path(record->path), record->size)'),
    ('port/src/main.cpp', 'int main(int argc, const char **argv, const char **envp) {\n    argc = PathsConsumeArgs(argc, argv);',
     'extern "C" char *setlocale(int, const char *);\nvoid InstallCrashHandler();\nint main(int argc, const char **argv, const char **envp) {\n    InstallCrashHandler();\n    setlocale(0 /* LC_ALL */, ".UTF-8"); // wide<->narrow path conversion needs a UTF-8 locale on Windows\n    argc = PathsConsumeArgs(argc, argv);\n    argc = SetupConsumeArgs(argc, argv);'),
    ('port/src/main.cpp', '    FirstRunIfNoData(options.headless);', '    FirstRunIfNoData(options.headless);\n    SetupIfNeeded(options.headless);'),
    ('port/src/main.cpp', 'void InstallCrashHandler();', 'void InstallCrashHandler();\nint SetupConsumeArgs(int, const char **);\nvoid SetupIfNeeded(bool);'),
    # Windows defaults: mailbox presenting keeps the game ticking at 50 Hz; a frame cap stops the GPU running flat out.
    ('port/src/platform/config.hpp', 'present_mode = ConfigPresentMode::Fifo;', 'present_mode = ConfigPresentMode::Mailbox;'),
    ('port/src/platform/config.hpp', 'max_fps = 0.0;', 'max_fps = 144.0;'),
    ('port/src/platform/config.hpp', 'show_fps = true;', 'show_fps = false;'),
    ('port/src/platform/config.hpp', 'debug_mode = true;', 'debug_mode = false;'),
    ('port/src/gfx/draw.cpp', 'Error("a draw samples the target it renders to; snapshot it first");',
     '{ static int dbg_n = 0; if (dbg_n++ < 8) Error("a draw samples the target it renders to (texture %#x, target %#x, main %#x, #%d); snapshot it first", (unsigned) binding.texture, (unsigned) g.target, (unsigned) kMainTarget, dbg_n); }'),
    # Mod framework phase 1a: texture overrides.
    ('port/src/texture.cpp', '#include "texture_port.hpp"', '#include "texture_port.hpp"\n#include "platform/mods.hpp"'),
    ('port/src/texture.cpp', '            return;\n        }\n        tbp = PortCreateTexture(decoded, PortTextureOwner::Manager, &cbp);',
     '            return;\n        }\n        { bool idx = decoded.format == gfx::TextureFormat::Index8; ModsTextureHook(name, bpp, block, decoded.width, decoded.height, idx, decoded.has_alpha, decoded.four_bit, decoded.palette.data(), decoded.levels); if (!idx) decoded.format = gfx::TextureFormat::Rgba8; }\n        tbp = PortCreateTexture(decoded, PortTextureOwner::Manager, &cbp);'),
    ('port/src/dataread.cpp', '#include "platform/paths.hpp"', '#include "platform/paths.hpp"\n#include "platform/mods.hpp"'),
    ('port/src/dataread.cpp', 'int LoadFile2(char *path, void *buffer, int *out_size, int mode) {\n    if (out_size) {',
     'int LoadFile2(char *path, void *buffer, int *out_size, int mode) {\n    ModsNoteFile(path);\n    if (out_size) {'),
]

def apply_patches():
    bad = 0
    for rel, old, new in PATCHES:
        f = DST / rel
        t = f.read_bytes().decode('utf-8', errors='surrogateescape')
        if old not in t:
            print(f'PATCH MISMATCH {rel}: {old[:60]!r}'); bad += 1; continue
        f.write_bytes(t.replace(old, new).encode('utf-8', errors='surrogateescape'))
    print(f'applied {len(PATCHES) - bad}/{len(PATCHES)} patches')

def main():
    if DST.exists():
        try:
            shutil.rmtree(DST)
        except PermissionError:
            print('note: could not clear the old tree (no delete permission); overwriting in place')
    for sub in ('ps2/src', 'ps2/include', 'port/include', 'port/src'):
        shutil.copytree(SRC / sub, DST / sub, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('tests') if sub == 'port/src' else None)
    total, files = 0, []
    for p in sorted(DST.rglob('*')):
        if not p.is_file() or p.suffix not in CODE_EXT:
            continue
        rel = p.relative_to(DST).as_posix()
        if rel.startswith(HOST_DIRS):
            continue
        text = p.read_bytes().decode('utf-8', errors='surrogateescape')
        new, n = rewrite_long(text)
        if n:
            p.write_bytes(new.encode('utf-8', errors='surrogateescape'))
            total += n; files.append((n, rel))
    # Windows replacements
    (DST / 'tools/dcdata').mkdir(parents=True, exist_ok=True)
    for name in ('dcdata.hpp', 'main.cpp'):
        shutil.copy(SRC / 'tools/dcdata' / name, DST / 'tools/dcdata' / name)
    shutil.copy(WIN / 'memory_win.cpp', DST / 'port/src/platform/memory.cpp')
    shutil.copy(WIN / 'crash_win.cpp', DST / 'port/src/platform/crash_win.cpp')
    shutil.copy(WIN / 'mods_win.cpp', DST / 'port/src/platform/mods.cpp')
    shutil.copy(WIN / 'mods.hpp', DST / 'port/src/platform/mods.hpp')
    shutil.copy(WIN / 'setup_win.cpp', DST / 'port/src/platform/setup.cpp')
    shutil.copy(WIN / 'CMakeLists.win.txt', DST / 'CMakeLists.txt')
    (DST / 'win').mkdir(parents=True, exist_ok=True)
    shutil.copy(WIN / 'weakcc.py', DST / 'win/weakcc.py')
    apply_patches()
    print(f'generated {DST}')
    print(f'long rewritten at {total} sites in {len(files)} files:')
    for n, rel in sorted(files, reverse=True)[:25]:
        print(f'  {n:4d} {rel}')

main()
