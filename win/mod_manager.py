#!/usr/bin/env python3
"""Chronicle mod manager: enable/disable mods, set load order, see conflicts.

    python mod_manager.py                 window (needs tkinter)
    python mod_manager.py --check         print the mods, their load order and every conflict, then exit
    python mod_manager.py --mods <dir>    mods folder (default: win-save/mods next to this script or its parent)

It edits only `mod.json` ("enabled", other keys kept) and `mods/load_order.json` ({"order": [...]}). Restart the game after saving.
Mods later in the order win a conflict (they load last). Mods that are not in load_order.json load first, alphabetically.
"""
import json, os, re, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def find_mods_dir(arg=None):
    if arg:
        return Path(arg)
    for base in (HERE, HERE.parent, HERE.parent.parent):
        for rel in ('win-save/mods', 'save/mods', 'mods'):
            if (base / rel).is_dir():
                return base / rel
    return HERE / 'mods'


def read_json(path, default):
    try:
        with open(path, encoding='utf-8') as f:
            data = json.load(f)
        return data
    except (OSError, ValueError):
        return default


def strip_json_comments(text):
    # the game accepts // and /* */ comments in its JSON; so do we
    out, i, n, in_str = [], 0, len(text), False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == '\\' and i + 1 < n:
                out.append(text[i + 1]); i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True; out.append(c)
        elif text.startswith('//', i):
            while i < n and text[i] != '\n':
                i += 1
            continue
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        else:
            out.append(c)
        i += 1
    return ''.join(out)


def read_loose_json(path):
    try:
        return json.loads(strip_json_comments(Path(path).read_text(encoding='utf-8-sig')))
    except (OSError, ValueError):
        return None


class Mod:
    def __init__(self, path):
        self.path = Path(path)
        self.name = self.path.name
        meta = read_json(self.path / 'mod.json', {})
        self.meta = meta if isinstance(meta, dict) else {}
        self.enabled = bool(self.meta.get('enabled', True))
        self.description = str(self.meta.get('description', ''))
        self.keys = {}          # kind -> set of keys this mod provides
        self.events = set()     # Lua events it handles
        self.problems = []
        self.scan()

    def scan(self):
        k = {'texture': set(), 'model': set(), 'data': set(), 'text': set(), 'file': set()}
        files = self.path / 'files'
        if files.is_dir():
            for p in files.rglob('*'):
                if p.is_file():
                    k['file'].add(p.relative_to(files).as_posix().lower())
        tex = self.path / 'textures'
        if tex.is_dir():
            for p in tex.rglob('*'):
                if p.suffix.lower() == '.png':
                    k['texture'].add(p.stem.lower())
        mod = self.path / 'models'
        if mod.is_dir():
            for p in mod.iterdir():
                if p.suffix.lower() in ('.obj', '.glb'):
                    k['model'].add(re.sub(r'__\d+v$', '', p.stem).lower())
        data = self.path / 'data'
        if data.is_dir():
            for p in sorted(data.glob('*.json')):
                j = read_loose_json(p)
                if not isinstance(j, dict):
                    self.problems.append(f'data/{p.name} is not valid JSON')
                    continue
                for table, entries in j.items():
                    if table.startswith('_') or not isinstance(entries, dict):
                        continue
                    for id_, val in entries.items():
                        if id_.startswith('_'):
                            continue
                        if isinstance(val, dict):
                            for field in val:
                                if not field.startswith('_'):
                                    k['data'].add(f'{table} {id_} {field}')
                        else:
                            k['data'].add(f'{table} {id_}')
        text = self.path / 'text'
        if text.is_dir():
            for p in sorted(text.glob('*.json')):
                j = read_loose_json(p)
                if not isinstance(j, dict):
                    self.problems.append(f'text/{p.name} is not valid JSON')
                    continue
                for pattern, entries in j.items():
                    if pattern.startswith('_') or not isinstance(entries, dict):
                        continue
                    for id_ in entries:
                        if not id_.startswith('_'):
                            k['text'].add(f'{pattern.lower()} #{id_}')
        self.keys = k
        lua = self.path / 'scripts' / 'main.lua'
        self.has_lua = lua.is_file()
        if self.has_lua:
            try:
                src = lua.read_text(encoding='utf-8', errors='replace')
                self.events = set(re.findall(r'dc\.on\(\s*["\']([a-z_]+)["\']', src))
            except OSError:
                pass
        self.has_native = (self.path / 'plugin.dll').is_file()

    def summary(self):
        parts = []
        for kind, label in (('texture', 'tex'), ('model', 'model'), ('data', 'data'), ('text', 'text'), ('file', 'file')):
            if self.keys[kind]:
                parts.append(f'{len(self.keys[kind])} {label}')
        if self.has_lua:
            parts.append('lua')
        if self.has_native:
            parts.append('native')
        return ', '.join(parts) or 'empty'


# Events whose handlers change a value; two mods doing it stack rather than replace (informational).
CHANGING = {'monster_hit': 'damage dealt', 'player_damage': 'damage taken', 'player_heal': 'healing', 'item_pickup': 'picked-up item'}


def load_order(mods_dir):
    data = read_json(Path(mods_dir) / 'load_order.json', {})
    order = data.get('order', []) if isinstance(data, dict) else []
    return [str(x) for x in order if isinstance(x, str)]


def scan(mods_dir):
    mods_dir = Path(mods_dir)
    mods = [Mod(p) for p in mods_dir.iterdir() if p.is_dir() and not p.name.startswith(('_', '.'))] if mods_dir.is_dir() else []
    order = [o.lower() for o in load_order(mods_dir)]

    def rank(m):
        return (order.index(m.name.lower()) if m.name.lower() in order else -1, m.name.lower())

    mods.sort(key=rank)
    return mods


def conflicts(mods):
    """[(kind, key, [mod names in load order], winner)] for keys provided by more than one ENABLED mod."""
    seen, out = {}, []
    for m in mods:
        if not m.enabled:
            continue
        for kind, keys in m.keys.items():
            for key in keys:
                seen.setdefault((kind, key), []).append(m.name)
    for (kind, key), names in sorted(seen.items()):
        if len(names) > 1:
            out.append((kind, key, names, names[-1]))
    notes = []
    for ev, what in CHANGING.items():
        names = [m.name for m in mods if m.enabled and ev in m.events]
        if len(names) > 1:
            notes.append(f'{ev}: {", ".join(names)} all change the {what}; the changes apply one after another in load order')
    return out, notes


def save(mods_dir, mods):
    """mods: list of Mod in the order wanted. Writes mod.json (enabled) and load_order.json."""
    mods_dir = Path(mods_dir)
    for m in mods:
        meta = dict(m.meta)
        if bool(meta.get('enabled', True)) != m.enabled or (m.path / 'mod.json').is_file():
            meta['enabled'] = m.enabled
            (m.path / 'mod.json').write_text(json.dumps(meta, indent=1) + '\n', encoding='utf-8')
        m.meta = meta
    (mods_dir / 'load_order.json').write_text(json.dumps({'order': [m.name for m in mods]}, indent=1) + '\n', encoding='utf-8')


def report(mods_dir):
    mods = scan(mods_dir)
    print(f'mods folder: {mods_dir}')
    print('load order (first loads first, last wins):')
    for i, m in enumerate(mods, 1):
        print(f'  {i:2d}. [{"x" if m.enabled else " "}] {m.name:24s} {m.summary()}')
        for p in m.problems:
            print(f'        ! {p}')
    cs, notes = conflicts(mods)
    if not cs and not notes:
        print('no conflicts between enabled mods')
    for kind, key, names, winner in cs:
        print(f'  conflict {kind:7s} {key}: {" < ".join(names)}  -> {winner} wins')
    for n in notes:
        print(f'  note {n}')
    return mods, cs


def gui(mods_dir):
    import tkinter as tk
    from tkinter import ttk, messagebox

    root = tk.Tk()
    root.title('Chronicle mod manager')
    root.geometry('860x560')
    state = {'mods': scan(mods_dir), 'dirty': False}

    top = ttk.Frame(root, padding=8)
    top.pack(fill='both', expand=True)
    ttk.Label(top, text=f'{mods_dir}   (later in the list wins a conflict)').pack(anchor='w')
    cols = ('on', 'name', 'what', 'conf')
    tree = ttk.Treeview(top, columns=cols, show='headings', selectmode='browse', height=10)
    for c, t, w in (('on', 'On', 40), ('name', 'Mod', 190), ('what', 'Contains', 420), ('conf', 'Conflicts', 80)):
        tree.heading(c, text=t)
        tree.column(c, width=w, anchor='w' if c in ('name', 'what') else 'center')
    tree.pack(fill='x', pady=6)
    ttk.Label(top, text='Conflicts between enabled mods').pack(anchor='w')
    box = tk.Text(top, height=14, wrap='none')
    box.pack(fill='both', expand=True)
    status = ttk.Label(top, text='')
    status.pack(anchor='w', pady=(4, 0))

    def counts():
        cs, _ = conflicts(state['mods'])
        n = {}
        for _, _, names, _ in cs:
            for nm in names:
                n[nm] = n.get(nm, 0) + 1
        return n

    def refresh(select=None):
        n = counts()
        tree.delete(*tree.get_children())
        for m in state['mods']:
            tree.insert('', 'end', iid=m.name, values=('x' if m.enabled else '', m.name, m.summary() + ('   (' + m.description + ')' if m.description else ''), n.get(m.name, '') or ''))
        if select and tree.exists(select):
            tree.selection_set(select)
        cs, notes = conflicts(state['mods'])
        box.configure(state='normal')
        box.delete('1.0', 'end')
        if not cs and not notes:
            box.insert('end', 'None.\n')
        for kind, key, names, winner in cs:
            box.insert('end', f'{kind:8s} {key}\n    {" < ".join(names)}   ->  {winner} wins\n')
        for t in notes:
            box.insert('end', f'note: {t}\n')
        for m in state['mods']:
            for p in m.problems:
                box.insert('end', f'problem in {m.name}: {p}\n')
        box.configure(state='disabled')
        status.configure(text='unsaved changes: press Save, then restart the game' if state['dirty'] else 'saved')

    def selected():
        s = tree.selection()
        return next((m for m in state['mods'] if s and m.name == s[0]), None)

    def toggle():
        m = selected()
        if m:
            m.enabled = not m.enabled
            state['dirty'] = True
            refresh(m.name)

    def move(d):
        m = selected()
        if not m:
            return
        i = state['mods'].index(m)
        j = i + d
        if 0 <= j < len(state['mods']):
            state['mods'][i], state['mods'][j] = state['mods'][j], state['mods'][i]
            state['dirty'] = True
            refresh(m.name)

    def do_save():
        try:
            save(mods_dir, state['mods'])
        except OSError as e:
            messagebox.showerror('Save failed', str(e))
            return
        state['dirty'] = False
        refresh(selected().name if selected() else None)

    def reload():
        state['mods'] = scan(mods_dir)
        state['dirty'] = False
        refresh()

    bar = ttk.Frame(top)
    bar.pack(fill='x', pady=4)
    for text, cmd in (('Enable / disable', toggle), ('Move up', lambda: move(-1)), ('Move down', lambda: move(1)), ('Save', do_save),
                      ('Reload', reload), ('Open folder', lambda: os.startfile(str(mods_dir)) if hasattr(os, 'startfile') else None)):
        ttk.Button(bar, text=text, command=cmd).pack(side='left', padx=3)
    tree.bind('<Double-1>', lambda e: toggle())
    refresh()

    def on_close():
        if state['dirty'] and not messagebox.askyesno('Unsaved changes', 'Quit without saving?'):
            return
        root.destroy()

    root.protocol('WM_DELETE_WINDOW', on_close)
    if '--selftest' in sys.argv:
        root.after(300, root.destroy)
    root.mainloop()


def main(argv):
    mods_arg = None
    if '--mods' in argv:
        mods_arg = argv[argv.index('--mods') + 1]
    mods_dir = find_mods_dir(mods_arg)
    if '--check' in argv:
        report(mods_dir)
        return 0
    try:
        gui(mods_dir)
    except ImportError:
        print('tkinter is not available in this Python; use --check, or install the standard Windows Python.')
        report(mods_dir)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
