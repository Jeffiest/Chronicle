#!/usr/bin/env python3
"""Chronicle launcher: one window to set up the game, co-op and mods, and start it.

    python launcher.py                 window
    python launcher.py --selftest      open the window, then close it (smoke test)
    python launcher.py --play          start the game with the saved settings, no window
    ChronicleLauncher.exe              the same, built by build_launcher.ps1 (PyInstaller)

Tabs: Game (data/save folders, window size, volume), Multiplayer (host / join with IP and port; it switches the co-op mod on and hands
the choice to the game through DC_LAUNCH_* environment variables), Mods (the mod manager: on/off, load order, conflicts).
Settings are remembered in launcher.json beside the launcher.
"""
import json, os, socket, subprocess, sys
from pathlib import Path

import mod_manager as mm

FROZEN = getattr(sys, 'frozen', False)
BASE = Path(sys.executable).resolve().parent if FROZEN else Path(__file__).resolve().parent
if not FROZEN and not (BASE / 'win-save').is_dir() and (BASE.parent.parent / 'win-save').is_dir():
    BASE = BASE.parent.parent            # run from Chronicle-src\win inside the project
CFG_PATH = BASE / 'launcher.json'
SIZES = ['1280x720', '1600x900', '1920x1080', '2560x1440', '3840x2160']
WSL_DATA = Path(r'\\wsl.localhost\Ubuntu\home\jeffie\Chronicle\data')


def find_exe():
    for rel in ('darkcloud.exe', 'Chronicle-win-build/darkcloud.exe', 'bin/darkcloud.exe'):
        if (BASE / rel).is_file():
            return BASE / rel
    return None


def default_save():
    for rel in ('win-save', 'save'):
        if (BASE / rel).is_dir():
            return BASE / rel
    return BASE / 'save'


def default_data():
    for p in (BASE / 'data', WSL_DATA):
        try:
            if p.is_dir():
                return p
        except OSError:
            pass
    return BASE / 'data'


DEFAULTS = {'data': '', 'save': '', 'size': '1280x720', 'fullscreen': False, 'max_fps': 240, 'volume': 50,
            'net_mode': 'off', 'net_ip': '127.0.0.1', 'net_port': 7777, 'tunic': 0, 'tunic_front': '', 'tunic_back': ''}


def load_cfg():
    c = dict(DEFAULTS)
    try:
        c.update(json.loads(CFG_PATH.read_text(encoding='utf-8')))
    except (OSError, ValueError):
        pass
    c['data'] = c['data'] or str(default_data())
    c['save'] = c['save'] or str(default_save())
    return c


def save_cfg(c):
    try:
        CFG_PATH.write_text(json.dumps(c, indent=1) + '\n', encoding='utf-8')
    except OSError:
        pass


def local_addresses():
    out = []
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('10.255.255.255', 1))       # no packet is sent; this picks the interface a LAN peer would reach
        out.append(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    try:
        for ip in socket.gethostbyname_ex(socket.gethostname())[2]:
            if ip not in out and not ip.startswith('127.'):
                out.append(ip)
    except OSError:
        pass
    return out


def valid_port(text):
    try:
        n = int(text)
    except ValueError:
        return None
    return n if 1 <= n <= 65535 else None


def write_game_config(save_dir, size, fullscreen, max_fps, volume):
    """Merge the launcher's choices into the game's config.json, keeping every other key."""
    path = Path(save_dir) / 'config.json'
    try:
        cfg = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        cfg = {}
    w, h = (int(x) for x in size.split('x'))
    cfg.setdefault('video', {}).update({'width': w, 'height': h, 'fullscreen': bool(fullscreen), 'max_fps': float(max_fps)})
    cfg['video'].setdefault('present_mode', 'mailbox')       # a new config starts on mailbox, never vsync; an existing choice is kept
    cfg.setdefault('audio', {})['master_volume'] = round(volume / 100.0, 3)
    Path(save_dir).mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(cfg, indent=4) + '\n', encoding='utf-8')


def set_coop(mods_dir, on):
    """Turn the co-op mod on or off in mod.json (other keys kept). Returns True when it exists."""
    p = Path(mods_dir) / 'coop' / 'mod.json'
    if not p.is_file():
        return False
    meta = mm.read_json(p, {})
    if not isinstance(meta, dict):
        return False
    if bool(meta.get('enabled', True)) != on:
        meta['enabled'] = on
        p.write_text(json.dumps(meta, indent=1) + '\n', encoding='utf-8')
    return True


TUNICS = ['Orange (original)', 'Blue', 'Green', 'Red', 'Purple', 'Teal', 'Pink', 'Yellow', 'Lime', 'Cyan', 'Magenta', 'Brown', 'White', 'Black', 'Gold', 'Navy']
TUNIC_RGB = ['#d9791f', '#2f6fe0', '#2fae4a', '#d62b2b', '#8a3fcf', '#1fa8a0', '#f06ab0', '#e8d22a', '#8fd62b', '#33c9ee', '#d12fc4', '#7a4a26', '#f2f2f2', '#2a2a2a', '#e0b02a', '#1c2a6b']
TEMPLATES = ('c01d04', 'c01d05')    # the poncho front and back textures (256x256) a custom design replaces


def prepare_tunic_picture(src, dest):
    """Shrinks or stretches the player's picture to 256x256 and saves it as a PNG the game can take. Returns an error text or None."""
    try:
        from PIL import Image
        im = Image.open(src).convert('RGBA').resize((256, 256), Image.LANCZOS)
        im.save(dest, 'PNG', optimize=True)
        if Path(dest).stat().st_size > 390000:
            return 'That picture is too detailed to send to other players (over 390 KB); try a simpler one.'
    except Exception as e:                      # unreadable file, no Pillow, disk error
        return f'Could not use the tunic picture {src}: {e}'
    return None


def export_tunic_templates(save_dir, dest_dir):
    """Copies the game's poncho textures (from the texture dump) so they can be painted over. Returns the files copied."""
    import shutil
    got = []
    for name in TEMPLATES:
        for cand in (Path(save_dir) / 'mods' / '_dump' / 'by_name' / (name + '.png'),):
            if cand.is_file():
                Path(dest_dir).mkdir(parents=True, exist_ok=True)
                shutil.copy(cand, Path(dest_dir) / (name + '.png'))
                got.append(name + '.png')
    return got


def start_game(c, log=None):
    """Writes the config, sets the co-op mod and environment, starts darkcloud.exe. Returns (process, message)."""
    exe = find_exe()
    if exe is None:
        return None, 'darkcloud.exe not found next to the launcher (or in Chronicle-win-build).'
    # A missing data folder is fine: the game itself then asks for the disc image and extracts it there.
    save = Path(c['save'])
    port = valid_port(c['net_port'])
    if c['net_mode'] != 'off' and port is None:
        return None, 'The port must be a number from 1 to 65535.'
    if c['net_mode'] == 'join' and not str(c['net_ip']).strip():
        return None, 'Enter the host\'s IP address to join.'
    write_game_config(save, c['size'], c['fullscreen'], c['max_fps'], c['volume'])
    if not set_coop(save / 'mods', c['net_mode'] != 'off') and c['net_mode'] != 'off':
        return None, 'The co-op mod is not in the mods folder:\n' + str(save / 'mods')
    env = dict(os.environ)
    for k in [k for k in env if k.startswith('DC_LAUNCH_')]:
        del env[k]
    tunic = int(c.get('tunic', 0) or 0)
    if tunic == 16:
        pics = {}
        for key, field in (('FRONT', 'tunic_front'), ('BACK', 'tunic_back')):
            if c.get(field):
                err = prepare_tunic_picture(c[field], save / f'launcher_tunic_{key.lower()}.png')
                if err:
                    return None, err
                pics[key] = save / f'launcher_tunic_{key.lower()}.png'
        if not pics:
            return None, 'Custom tunic is selected but no picture is chosen (Multiplayer tab).'
        for key, path in pics.items():
            env['DC_LAUNCH_TUNIC_' + key] = str(path)
    env['DC_LAUNCH_TUNIC'] = str(tunic)
    if c['net_mode'] != 'off':
        env['DC_LAUNCH_COOP'] = c['net_mode']
        env['DC_LAUNCH_PORT'] = str(port)
        env['DC_LAUNCH_IP'] = str(c['net_ip']).strip()
    w, h = c['size'].split('x')
    args = [str(exe), '--data', c['data'], '--save', str(save), '--width', w, '--height', h]
    out = open(log, 'wb') if log else subprocess.DEVNULL
    flags = 0x08000000 if os.name == 'nt' else 0            # CREATE_NO_WINDOW: no console flashes up behind the game
    proc = subprocess.Popen(args, cwd=str(exe.parent), env=env, stdout=out, stderr=subprocess.STDOUT, creationflags=flags)
    return proc, 'started'


BG, SIDE, CARD, EDGE = '#17120f', '#120e0b', '#211a16', '#33271f'
FG, MUTED, ORANGE, BLUE, FIELD = '#ece4d8', '#9d9183', '#f5a623', '#3d9bff', '#2b221c'


def apply_theme(root):
    from tkinter import ttk
    st = ttk.Style(root)
    st.theme_use('clam')
    root.configure(bg=BG)
    root.option_add('*Text.background', FIELD)
    root.option_add('*Text.foreground', FG)
    root.option_add('*Text.relief', 'flat')
    root.option_add('*TCombobox*Listbox.background', FIELD)
    root.option_add('*TCombobox*Listbox.foreground', FG)
    root.option_add('*TCombobox*Listbox.selectBackground', ORANGE)
    root.option_add('*TCombobox*Listbox.selectForeground', '#111')
    st.configure('.', background=CARD, foreground=FG, fieldbackground=FIELD, bordercolor=EDGE, lightcolor=EDGE, darkcolor=EDGE,
                 troughcolor=FIELD, focuscolor=CARD, font=('Segoe UI', 10))
    st.configure('TFrame', background=CARD)
    st.configure('TLabel', background=CARD, foreground=FG)
    st.configure('Muted.TLabel', foreground=MUTED)
    st.configure('Head.TLabel', foreground=ORANGE, font=('Segoe UI', 12, 'bold'))
    st.configure('TEntry', fieldbackground=FIELD, foreground=FG, insertcolor=FG, padding=5)
    st.configure('TCombobox', fieldbackground=FIELD, foreground=FG, arrowcolor=ORANGE, padding=4)
    st.map('TCombobox', fieldbackground=[('readonly', FIELD)], foreground=[('readonly', FG)])
    st.configure('TButton', background=FIELD, foreground=FG, padding=(12, 6), borderwidth=1, relief='flat')
    st.map('TButton', background=[('active', EDGE), ('disabled', CARD)], foreground=[('disabled', MUTED)])
    st.configure('TCheckbutton', background=CARD, foreground=FG)
    st.configure('TRadiobutton', background=CARD, foreground=FG, padding=3)
    st.map('TCheckbutton', background=[('active', CARD)], indicatorcolor=[('selected', ORANGE)])
    st.map('TRadiobutton', background=[('active', CARD)], indicatorcolor=[('selected', ORANGE)])
    st.configure('Horizontal.TScale', background=CARD, troughcolor=FIELD)
    st.configure('Treeview', background=FIELD, fieldbackground=FIELD, foreground=FG, rowheight=24, borderwidth=0)
    st.map('Treeview', background=[('selected', '#5a3d12')], foreground=[('selected', '#ffffff')])
    st.configure('Treeview.Heading', background=EDGE, foreground=ORANGE, relief='flat', font=('Segoe UI', 10, 'bold'))
    st.configure('Vertical.TScrollbar', background=EDGE, troughcolor=FIELD, arrowcolor=MUTED)


def gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    c = load_cfg()
    root = tk.Tk()
    root.title('Chronicle Launcher')
    root.geometry('1000x800')
    root.minsize(900, 700)
    apply_theme(root)
    state = {'proc': None}
    v = {k: tk.StringVar(value=str(c[k])) for k in ('data', 'save', 'size', 'max_fps', 'net_ip', 'net_port')}
    v['fullscreen'] = tk.BooleanVar(value=bool(c['fullscreen']))
    v['volume'] = tk.IntVar(value=int(c['volume']))
    v['net_mode'] = tk.StringVar(value=c['net_mode'])
    v['tunic'] = tk.IntVar(value=int(c['tunic']))
    v['tunic_front'] = tk.StringVar(value=c['tunic_front'])
    v['tunic_back'] = tk.StringVar(value=c['tunic_back'])

    def collect():
        out = {k: v[k].get() for k in v}
        out['fullscreen'] = bool(out['fullscreen'])
        try:
            out['volume'] = int(float(out['volume']))
            out['max_fps'] = float(out['max_fps'])
        except ValueError:
            out['max_fps'] = 240.0
        out['net_port'] = out['net_port'].strip()
        out['tunic'] = int(v['tunic'].get())
        return out

    # layout: sidebar | content, status bar under the content
    side = tk.Frame(root, bg=SIDE, width=230)
    side.pack(side='left', fill='y')
    side.pack_propagate(False)
    main = tk.Frame(root, bg=BG)
    main.pack(side='left', fill='both', expand=True, padx=16, pady=16)
    tk.Label(side, text='DARK CLOUD', bg=SIDE, fg=ORANGE, font=('Segoe UI', 20, 'bold')).pack(anchor='w', padx=22, pady=(24, 0))
    tk.Label(side, text='- CHRONICLE  WINDOWS -', bg=SIDE, fg=BLUE, font=('Segoe UI', 9, 'bold')).pack(anchor='w', padx=24, pady=(0, 18))

    def flat_button(parent, text, cmd, big=False):
        bg, fg = (ORANGE, '#1a1208') if big else (CARD, MUTED)
        b = tk.Button(parent, text=text, command=cmd, bg=bg, fg=fg, activebackground='#ffc04d' if big else EDGE, activeforeground='#000' if big else FG,
                      relief='flat', bd=0, cursor='hand2', font=('Segoe UI', 14 if big else 11, 'bold' if big else 'normal'), pady=14 if big else 10,
                      highlightthickness=1, highlightbackground='#ffb84a' if big else EDGE)
        b.pack(fill='x', padx=18, pady=(0, 8))
        return b

    play = flat_button(side, 'Start Game', None, big=True)
    pages, navs = {}, {}
    name_labels = {'files': 'Game Files', 'settings': 'Settings', 'multi': 'Multiplayer', 'char': 'Character', 'mods': 'Mods'}

    def show(name):
        for n, f in pages.items():
            f.pack_forget()
            navs[n].configure(bg=CARD, fg=MUTED)
        pages[name].pack(fill='both', expand=True)
        navs[name].configure(bg=EDGE, fg=ORANGE)

    def page(name, title, subtitle):
        f = tk.Frame(main, bg=BG)
        pages[name] = f
        head = tk.Frame(f, bg='#f3efe6')
        head.pack(fill='x', pady=(0, 12))
        tk.Label(head, text=title, bg='#f3efe6', fg='#161616', font=('Segoe UI', 17, 'bold')).pack(anchor='w', padx=18, pady=(12, 0))
        tk.Label(head, text=subtitle, bg='#f3efe6', fg='#5a5148', font=('Segoe UI', 10)).pack(anchor='w', padx=18, pady=(0, 12))
        navs[name] = flat_button(side, name_labels[name], lambda n=name: show(n))
        return f

    def card(parent, heading=None):
        outer = tk.Frame(parent, bg=EDGE)
        outer.pack(fill='x', pady=(0, 12))
        inner = ttk.Frame(outer, padding=16)
        inner.pack(fill='both', expand=True, padx=1, pady=1)
        if heading:
            ttk.Label(inner, text=heading, style='Head.TLabel').grid(row=0, column=0, columnspan=4, sticky='w', pady=(0, 8))
        return inner

    def browse(var):
        return lambda: var.set(filedialog.askdirectory(initialdir=var.get() or str(BASE)) or var.get())

    # Game Files
    p = page('files', 'Game Files & Folders', 'Where the extracted game data and your saves live')
    cd = card(p, 'Game data folder')
    ttk.Entry(cd, textvariable=v['data']).grid(row=1, column=0, sticky='we')
    ttk.Button(cd, text='Browse...', command=browse(v['data'])).grid(row=1, column=1, padx=(8, 0))
    cd.columnconfigure(0, weight=1)
    cs = card(p, 'Save folder (settings, saves and mods)')
    ttk.Entry(cs, textvariable=v['save']).grid(row=1, column=0, sticky='we')
    ttk.Button(cs, text='Browse...', command=browse(v['save'])).grid(row=1, column=1, padx=(8, 0))
    ttk.Button(cs, text='Open', command=lambda: os.startfile(v['save'].get()) if Path(v['save'].get()).is_dir() else None).grid(row=1, column=2, padx=(8, 0))
    cs.columnconfigure(0, weight=1)
    exe = find_exe()
    ce = card(p, 'Game')
    ttk.Label(ce, style='Muted.TLabel', text=str(exe) if exe else 'darkcloud.exe not found: put the launcher beside it.').grid(row=1, column=0, sticky='w')

    # Settings
    p = page('settings', 'Settings', 'Video and sound; applied when you start the game')
    cv = card(p, 'Video')
    ttk.Label(cv, text='Window size').grid(row=1, column=0, sticky='w', pady=6)
    ttk.Combobox(cv, textvariable=v['size'], values=SIZES, width=14).grid(row=1, column=1, sticky='w', padx=12)
    ttk.Checkbutton(cv, text='Fullscreen', variable=v['fullscreen']).grid(row=2, column=1, sticky='w', padx=12, pady=4)
    ttk.Label(cv, text='Frame limit').grid(row=3, column=0, sticky='w', pady=6)
    ttk.Entry(cv, textvariable=v['max_fps'], width=8).grid(row=3, column=1, sticky='w', padx=12)
    ca = card(p, 'Audio')
    ttk.Label(ca, text='Volume').grid(row=1, column=0, sticky='w', pady=6)
    vol_text = ttk.Label(ca, text=str(v['volume'].get()) + '%', style='Muted.TLabel')
    ttk.Scale(ca, from_=0, to=100, variable=v['volume'], orient='horizontal', length=260,
              command=lambda x: vol_text.configure(text=str(int(float(x))) + '%')).grid(row=1, column=1, padx=12)
    vol_text.grid(row=1, column=2)

    # Multiplayer
    p = page('multi', 'Multiplayer', "See other players and share the host's dungeon floors and monsters")
    cm = card(p, 'Mode')
    for i, (val, text) in enumerate((('off', 'Single player'), ('host', 'Host a game'), ('join', 'Join a game'))):
        ttk.Radiobutton(cm, text=text, value=val, variable=v['net_mode']).grid(row=1, column=i, sticky='w', padx=(0, 24))
    cn = card(p, 'Connection')
    ttk.Label(cn, text='Host IP address').grid(row=1, column=0, sticky='w', pady=6)
    ip_entry = ttk.Entry(cn, textvariable=v['net_ip'], width=24)
    ip_entry.grid(row=1, column=1, sticky='w', padx=12)
    ttk.Label(cn, text='Port').grid(row=2, column=0, sticky='w', pady=6)
    port_entry = ttk.Entry(cn, textvariable=v['net_port'], width=8)
    port_entry.grid(row=2, column=1, sticky='w', padx=12)
    result = ttk.Label(cn, text='', style='Muted.TLabel', wraplength=640)

    def test_connection():
        port = valid_port(v['net_port'].get().strip())
        ip = v['net_ip'].get().strip()
        if port is None or not ip:
            result.configure(text='Enter an IP address and a port (1-65535) first.')
            return
        try:
            socket.create_connection((ip, port), timeout=3).close()
            result.configure(text=f'Reached {ip}:{port}: a host is listening.')
        except OSError as e:
            result.configure(text=f'Could not reach {ip}:{port} ({e.strerror or e}). The host must be running a game and allow the port through the firewall.')

    addrs = local_addresses()
    bf = ttk.Frame(cn)
    bf.grid(row=3, column=0, columnspan=3, sticky='w', pady=(8, 0))
    ttk.Button(bf, text='Test connection to host', command=test_connection).pack(side='left', padx=(0, 8))
    ttk.Button(bf, text='Copy my address', command=lambda: (root.clipboard_clear(), root.clipboard_append(f'{addrs[0] if addrs else "127.0.0.1"}:{v["net_port"].get()}'))).pack(side='left')
    result.grid(row=4, column=0, columnspan=3, sticky='w', pady=(8, 0))
    pm = p
    p = page('char', 'Character', 'Pick the colour of the tunic; other players see it in co-op')
    ct = card(p, 'Tunic colour')
    swatches = {}

    def pick_tunic(i):
        v['tunic'].set(i)
        for k, b in swatches.items():
            b.configure(highlightbackground=ORANGE if k == i else EDGE, highlightthickness=3 if k == i else 1)
        tunic_name.configure(text=(TUNICS[i] if i < 16 else 'Custom design'))

    grid = tk.Frame(ct, bg=CARD)
    grid.grid(row=1, column=0, columnspan=4, sticky='w')
    for i, col in enumerate(TUNIC_RGB):
        b = tk.Button(grid, bg=col, width=4, height=2, relief='flat', bd=0, cursor='hand2', command=lambda i=i: pick_tunic(i), activebackground=col,
                      highlightthickness=1, highlightbackground=EDGE)
        b.grid(row=i // 8, column=i % 8, padx=4, pady=4)
        swatches[i] = b
    cb = tk.Button(grid, text='Custom', bg=FIELD, fg=FG, width=8, height=2, relief='flat', bd=0, cursor='hand2', command=lambda: pick_tunic(16),
                   activebackground=EDGE, highlightthickness=1, highlightbackground=EDGE)
    cb.grid(row=2, column=0, columnspan=2, padx=4, pady=4, sticky='w')
    swatches[16] = cb
    tunic_name = ttk.Label(ct, text='', style='Muted.TLabel')
    tunic_name.grid(row=2, column=0, columnspan=4, sticky='w', pady=(4, 0))
    cu = ttk.Frame(ct)
    cu.grid(row=3, column=0, columnspan=4, sticky='we', pady=(8, 0))
    ttk.Label(cu, text='Custom design: your own pictures for the front and back of the poncho (any size, PNG; shared with other players).',
              style='Muted.TLabel', wraplength=640).grid(row=0, column=0, columnspan=3, sticky='w')
    for r, (label, key) in enumerate((('Front picture', 'tunic_front'), ('Back picture (optional)', 'tunic_back')), 1):
        ttk.Label(cu, text=label).grid(row=r, column=0, sticky='w', pady=3)
        ttk.Entry(cu, textvariable=v[key], width=48).grid(row=r, column=1, sticky='we', padx=8)
        ttk.Button(cu, text='Choose...', command=lambda key=key: (v[key].set(filedialog.askopenfilename(filetypes=[('Pictures', '*.png *.jpg *.bmp')]) or v[key].get()),
                                                                 pick_tunic(16))).grid(row=r, column=2)

    def do_export():
        folder = filedialog.askdirectory(title='Save the template pictures where?')
        if not folder:
            return
        got = export_tunic_templates(v['save'].get(), folder)
        messagebox.showinfo('Tunic template', ('Saved ' + ', '.join(got) + ' to ' + folder + '. Paint over them, then choose them above.') if got else
                            'The game\'s textures have not been exported yet. Start the game once with DC_DUMP_TEXTURES=1 (run_win.ps1 -DumpTextures), then try again.')

    ttk.Button(cu, text='Save the template pictures...', command=do_export).grid(row=3, column=1, sticky='w', padx=8, pady=(6, 0))
    pick_tunic(int(v['tunic'].get()))
    p = pm
    ch = card(p, 'If you host')
    ttk.Label(ch, text='Give friends one of these addresses (same network), or your Tailscale / VPN address:\n    ' + ('\n    '.join(addrs) or 'none found'), style='Muted.TLabel').grid(row=1, column=0, sticky='w')
    ttk.Label(ch, text='Allow the chosen TCP port through Windows Firewall (Windows asks the first time). Everyone needs the same mods in the same order.',
              style='Muted.TLabel', wraplength=640).grid(row=2, column=0, sticky='w', pady=(8, 0))

    def sync_net(*_):
        ip_entry.configure(state='normal' if v['net_mode'].get() == 'join' else 'disabled')
        port_entry.configure(state='normal' if v['net_mode'].get() != 'off' else 'disabled')

    v['net_mode'].trace_add('write', sync_net)
    sync_net()

    # Mods
    p = page('mods', 'Mods', 'Turn mods on and off and set their load order (later in the list wins a conflict)')
    mods_dir = Path(v['save'].get()) / 'mods'
    box = ttk.Frame(p)
    box.pack(fill='both', expand=True)
    panel = mm.panel(box, mods_dir) if mods_dir.is_dir() else None
    if panel is None:
        ttk.Label(box, text=f'No mods folder yet at {mods_dir}', padding=12).pack()

    # sidebar bottom, status bar
    tk.Frame(side, bg=SIDE).pack(fill='both', expand=True)

    def log_path():
        return Path(v['save'].get()) / 'launcher_run.log'

    def on_close():
        save_cfg(collect())
        if panel and panel['dirty']() and not messagebox.askyesno('Unsaved mod changes', 'Quit without saving the mod list?'):
            return
        root.destroy()

    flat_button(side, 'Open log', lambda: os.startfile(str(log_path())) if log_path().is_file() else None)
    flat_button(side, 'Exit', on_close)
    tk.Label(side, text='Chronicle for Windows', bg=SIDE, fg='#6b6054', font=('Segoe UI', 8)).pack(pady=(0, 10))
    sb = tk.Frame(main, bg=CARD, highlightthickness=1, highlightbackground=EDGE)
    sb.pack(side='bottom', fill='x', pady=(8, 0))
    status = tk.Label(sb, text='Ready', bg=CARD, fg=FG, anchor='w', font=('Segoe UI', 10))
    status.pack(fill='x', padx=14, pady=10)

    def poll():
        pr = state['proc']
        if pr is not None and pr.poll() is not None:
            status.configure(text=f'Game exited (code {pr.returncode}). See the log if it closed unexpectedly.')
            play.configure(state='normal', text='Start Game')
            state['proc'] = None
        root.after(700, poll)

    def do_play():
        cfg = collect()
        save_cfg(cfg)
        if panel and panel['dirty']():
            panel['save']()                      # mod list changes apply on start
        lp = log_path()
        try:
            lp.parent.mkdir(parents=True, exist_ok=True)
        except OSError:
            lp = None
        proc, msg = start_game(cfg, lp)
        if proc is None:
            messagebox.showerror('Cannot start', msg)
            return
        state['proc'] = proc
        play.configure(state='disabled', text='Running...')
        status.configure(text='Game running: ' + (cfg['net_mode'] if cfg['net_mode'] != 'off' else 'single player'))

    play.configure(command=do_play)
    root.protocol('WM_DELETE_WINDOW', on_close)
    show('files')
    poll()
    if '--selftest' in sys.argv:
        for i, n in enumerate(list(pages)):
            root.after(100 + 100 * i, lambda n=n: show(n))
        root.after(800, root.destroy)
    root.mainloop()


def main(argv):
    if '--play' in argv:
        proc, msg = start_game(load_cfg(), BASE / 'launcher_run.log')
        print(msg)
        return 0 if proc else 1
    gui()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
