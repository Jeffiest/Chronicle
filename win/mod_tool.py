"""Dark Cloud model replacer (Windows fork).

Pick a model the game has built (dumped with run_win.ps1 -DumpModels), choose your own .glb / .obj, check the preview,
press Install. The tool scales, orients and aligns your model to the original, converts it to the game's format and
puts it (and its texture) in a mod folder under win-save\\mods.

    python mod_tool.py [--save E:\\path\\to\\win-save]
Optional: pip install pillow (JPEG textures), tkinterdnd2 (drag and drop files onto the window).
"""
import json, math, os, re, struct, subprocess, sys

# ---------------------------------------------------------------------------------------------------- mesh handling
class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.tris = [], [], [], []   # parallel arrays, tris = list of (a, b, c)
        self.tri_group = []   # optional, parallel to tris: the original strip a face's material comes from (OBJ 'usemtl strip<N>')

def load_obj(path):
    P, N, T = [], [], []
    m = Mesh(); seen = {}
    any_n = False; group = -1; any_group = False
    for line in open(path, encoding='utf-8', errors='replace'):
        t = line.split()
        if not t: continue
        if t[0] == 'usemtl':
            mm = re.fullmatch(r'strip(\d+)', t[1]) if len(t) > 1 else None
            if mm: group = int(mm.group(1)); any_group = True
        elif t[0] == 'v': P.append(tuple(map(float, t[1:4])))
        elif t[0] == 'vn': N.append(tuple(map(float, t[1:4])))
        elif t[0] == 'vt': T.append(tuple(map(float, t[1:3])))
        elif t[0] == 'f':
            face = []
            for tok in t[1:]:
                parts = tok.split('/')
                def ix(s, n):
                    if not s: return -1
                    i = int(s); return i - 1 if i > 0 else n + i
                pi = ix(parts[0], len(P)); ti = ix(parts[1], len(T)) if len(parts) > 1 else -1
                ni = ix(parts[2], len(N)) if len(parts) > 2 else -1
                key = (pi, ti, ni)
                if key not in seen:
                    seen[key] = len(m.pos)
                    m.pos.append(P[pi])
                    if 0 <= ni < len(N): m.nrm.append(N[ni]); any_n = True
                    else: m.nrm.append((0.0, 0.0, 0.0))
                    # OBJ v points up, the game's down: keep the game's convention inside the tool
                    m.uv.append((T[ti][0], 1.0 - T[ti][1]) if 0 <= ti < len(T) else (0.0, 0.0))
                face.append(seen[key])
            for i in range(1, len(face) - 1):
                m.tris.append((face[0], face[i], face[i + 1])); m.tri_group.append(group)
    if not any_group: m.tri_group = []
    if not any_n: compute_normals(m)
    return m

def write_obj(m, path, header=''):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        if header: f.write('# ' + header + '\n')
        for p in m.pos: f.write('v %.6f %.6f %.6f\n' % p)
        for u in m.uv: f.write('vt %.6f %.6f\n' % (u[0], 1.0 - u[1]))
        for n in m.nrm: f.write('vn %.6f %.6f %.6f\n' % n)
        order = list(range(len(m.tris)))
        grouped = len(m.tri_group) == len(m.tris) and any(g >= 0 for g in m.tri_group)
        if grouped: order.sort(key=lambda i: (max(m.tri_group[i], 0), i))   # faces before any usemtl count as strip 0
        last = None
        for i in order:
            a, b, c = m.tris[i]
            if grouped and max(m.tri_group[i], 0) != last:
                last = max(m.tri_group[i], 0); f.write('usemtl strip%d\n' % last)
            f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (a+1,a+1,a+1,b+1,b+1,b+1,c+1,c+1,c+1))

def compute_normals(m):
    acc = [[0.0, 0.0, 0.0] for _ in m.pos]
    for a, b, c in m.tris:
        pa, pb, pc = m.pos[a], m.pos[b], m.pos[c]
        e1 = [pb[i] - pa[i] for i in range(3)]; e2 = [pc[i] - pa[i] for i in range(3)]
        n = (e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0])
        for v in (a, b, c):
            for i in range(3): acc[v][i] += n[i]
    m.nrm = []
    for n in acc:
        l = math.sqrt(sum(x*x for x in n)) or 1.0
        m.nrm.append((n[0]/l, n[1]/l, n[2]/l))

def load_glb(path):
    """Returns (Mesh, embedded image bytes or None, mime)."""
    data = open(path, 'rb').read()
    if data[:4] != b'glTF': raise ValueError('not a .glb file (export "glTF Binary" from Blender, Unity or Unreal)')
    at = 12; doc = None; binary = b''
    while at + 8 <= len(data):
        length, ctype = struct.unpack_from('<II', data, at); at += 8
        chunk = data[at:at + length]
        if ctype == 0x4E4F534A: doc = json.loads(chunk.decode('utf-8'))
        elif ctype == 0x004E4942 and not binary: binary = chunk
        at += (length + 3) & ~3
    if not doc or not doc.get('meshes'): raise ValueError('no mesh in this file')

    def accessor(i, comps):
        acc = doc['accessors'][i]; view = doc['bufferViews'][acc['bufferView']]
        off = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
        ct = acc['componentType']; size = {5126: 4, 5123: 2, 5121: 1, 5125: 4}[ct]
        fmt = {5126: 'f', 5123: 'H', 5121: 'B', 5125: 'I'}[ct]
        stride = view.get('byteStride', 0) or size * comps
        out = []
        for k in range(acc['count']):
            row = struct.unpack_from('<' + fmt * comps, binary, off + k * stride)
            if acc.get('normalized') and ct != 5126:
                row = tuple(x / (65535.0 if ct == 5123 else 255.0) for x in row)
            out.append(tuple(row))
        return out

    m = Mesh()
    for prim in doc['meshes'][0]['primitives']:
        if prim.get('mode', 4) != 4: continue
        attrs = prim['attributes']
        pos = accessor(attrs['POSITION'], 3)
        nrm = accessor(attrs['NORMAL'], 3) if 'NORMAL' in attrs else [(0.0, 0.0, 0.0)] * len(pos)
        uv = accessor(attrs['TEXCOORD_0'], 2) if 'TEXCOORD_0' in attrs else [(0.0, 0.0)] * len(pos)
        base = len(m.pos)
        m.pos += pos; m.nrm += nrm; m.uv += uv   # glTF v is image-down, like the game's
        idx = [i[0] for i in accessor(prim['indices'], 1)] if 'indices' in prim else list(range(len(pos)))
        for i in range(0, len(idx) - 2, 3): m.tris.append((base + idx[i], base + idx[i+1], base + idx[i+2]))
    if not m.tris: raise ValueError('the first mesh has no triangles')
    if all(n == (0.0, 0.0, 0.0) for n in m.nrm): compute_normals(m)
    image, mime = None, None
    try:
        mat = doc['meshes'][0]['primitives'][0].get('material')
        if mat is not None:
            tex = doc['materials'][mat]['pbrMetallicRoughness']['baseColorTexture']['index']
            img = doc['images'][doc['textures'][tex]['source']]
            view = doc['bufferViews'][img['bufferView']]
            off = view.get('byteOffset', 0); image = binary[off:off + view['byteLength']]; mime = img.get('mimeType')
    except (KeyError, IndexError, TypeError):
        pass
    return m, image, mime

def load_dae(path):
    """Collada (.dae): every geometry instance in the visual scene, node transforms applied, plus the diffuse image."""
    import xml.etree.ElementTree as ET
    root = ET.parse(path).getroot()
    local = lambda e: e.tag.rsplit('}', 1)[-1]
    kids = lambda e, n: [c for c in e if local(c) == n]
    every = lambda e, n: [x for x in e.iter() if local(x) == n]
    ident = [[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]
    def mmul(a, b): return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
    def floats(txt): return [float(x) for x in txt.split()]
    sources = {}
    for src in every(root, 'source'):
        arr = kids(src, 'float_array')
        if not arr: continue
        stride = 3
        for acc in every(src, 'accessor'): stride = int(acc.get('stride', stride))
        sources[src.get('id')] = (floats(arr[0].text or ''), stride)
    geoms = {}
    for g in every(root, 'geometry'):
        meshes = kids(g, 'mesh')
        if meshes: geoms[g.get('id')] = meshes[0]

    def build(meshel, M):
        verts_inputs = {}
        for v in kids(meshel, 'vertices'):
            verts_inputs[v.get('id')] = {i.get('semantic'): i.get('source')[1:] for i in kids(v, 'input')}
        out = Mesh(); seen = {}
        for prim in meshel:
            kind = local(prim)
            if kind not in ('triangles', 'polylist', 'polygons'): continue
            ins = [(i.get('semantic'), i.get('source')[1:], int(i.get('offset', 0)), int(i.get('set', 0))) for i in kids(prim, 'input')]
            stride = max(o for _, _, o, _ in ins) + 1
            vert = next(i for i in ins if i[0] == 'VERTEX')
            vmap = verts_inputs.get(vert[1], {})
            pos_src = sources[vmap['POSITION']]
            def pick(sem):
                for i in ins:
                    if i[0] == sem and i[3] == 0: return sources[i[1]], i[2]
                if sem in vmap: return sources[vmap[sem]], vert[2]
                return None, 0
            nrm_src, nrm_off = pick('NORMAL'); uv_src, uv_off = pick('TEXCOORD')
            if kind == 'triangles':
                p = [int(x) for x in kids(prim, 'p')[0].text.split()]
                polys = [p[i:i + 3*stride] for i in range(0, len(p), 3*stride)]
            elif kind == 'polylist':
                counts = [int(x) for x in kids(prim, 'vcount')[0].text.split()]
                p = [int(x) for x in kids(prim, 'p')[0].text.split()]
                polys = []; at = 0
                for c in counts: polys.append(p[at:at + c*stride]); at += c*stride
            else:
                polys = [[int(x) for x in q.text.split()] for q in kids(prim, 'p')]
            for poly in polys:
                face = []
                for c in range(len(poly) // stride):
                    corner = poly[c*stride:(c + 1)*stride]
                    pi, ni, ti = corner[vert[2]], (corner[nrm_off] if nrm_src else -1), (corner[uv_off] if uv_src else -1)
                    key = (pi, ni, ti)
                    if key not in seen:
                        seen[key] = len(out.pos)
                        pv = pos_src[0][pi*pos_src[1]:pi*pos_src[1] + 3]
                        out.pos.append(tuple(sum(M[r][k]*pv[k] for k in range(3)) + M[r][3] for r in range(3)))
                        if nrm_src:
                            nv = nrm_src[0][ni*nrm_src[1]:ni*nrm_src[1] + 3]
                            n = tuple(sum(M[r][k]*nv[k] for k in range(3)) for r in range(3))
                            l = math.sqrt(sum(x*x for x in n)) or 1.0
                            out.nrm.append((n[0]/l, n[1]/l, n[2]/l))
                        else: out.nrm.append((0.0, 0.0, 0.0))
                        if uv_src:
                            tv = uv_src[0][ti*uv_src[1]:ti*uv_src[1] + 2]
                            out.uv.append((tv[0], 1.0 - tv[1]))   # Collada t points up, the game's down
                        else: out.uv.append((0.0, 0.0))
                    face.append(seen[key])
                for i in range(1, len(face) - 1): out.tris.append((face[0], face[i], face[i + 1]))
        return out

    def node_matrix(node):
        M = ident
        for t in node:
            n = local(t)
            if n == 'matrix':
                f = floats(t.text); M = mmul(M, [f[0:4], f[4:8], f[8:12], f[12:16]])
            elif n == 'translate':
                f = floats(t.text); M = mmul(M, [[1,0,0,f[0]],[0,1,0,f[1]],[0,0,1,f[2]],[0,0,0,1]])
            elif n == 'scale':
                f = floats(t.text); M = mmul(M, [[f[0],0,0,0],[0,f[1],0,0],[0,0,f[2],0],[0,0,0,1]])
            elif n == 'rotate':
                f = floats(t.text); ax = f[:3]; l = math.sqrt(sum(x*x for x in ax)) or 1.0; x, y, z = (a/l for a in ax)
                a = f[3]*math.pi/180.0; c, s_ = math.cos(a), math.sin(a); u = 1 - c
                M = mmul(M, [[c+x*x*u, x*y*u-z*s_, x*z*u+y*s_, 0],[y*x*u+z*s_, c+y*y*u, y*z*u-x*s_, 0],[z*x*u-y*s_, z*y*u+x*s_, c+z*z*u, 0],[0,0,0,1]])
        return M

    result = Mesh()
    def add(m):
        base = len(result.pos)
        result.pos += m.pos; result.nrm += m.nrm; result.uv += m.uv
        result.tris += [(a + base, b + base, c + base) for a, b, c in m.tris]
    def walk(node, parent):
        M = mmul(parent, node_matrix(node))
        for inst in kids(node, 'instance_geometry'):
            mesh_el = geoms.get(inst.get('url', '')[1:])
            if mesh_el is not None: add(build(mesh_el, M))
        for child in kids(node, 'node'): walk(child, M)
    scenes = every(root, 'visual_scene')
    if scenes:
        for node in kids(scenes[0], 'node'): walk(node, ident)
    if not result.tris:   # no scene: take the geometry as it is
        for mesh_el in geoms.values(): add(build(mesh_el, ident))
    if not result.tris: raise ValueError('no triangles in this .dae')
    if all(n == (0.0, 0.0, 0.0) for n in result.nrm): compute_normals(result)
    # diffuse image: the first <image> next to the file
    image = None
    folder = os.path.dirname(os.path.abspath(path))
    for img in every(root, 'image'):
        ref = kids(img, 'init_from')
        if ref and ref[0].text:
            cand = os.path.join(folder, ref[0].text.strip().replace('file://', '').replace('/', os.sep))
            if os.path.exists(cand):
                image = open(cand, 'rb').read(); break
    return result, image, 'image/png'

def load_mesh(path):
    ext = os.path.splitext(path)[1].lower()
    if ext == '.glb': return load_glb(path)
    if ext == '.obj': return load_obj(path), None, None
    if ext == '.dae': return load_dae(path)
    raise ValueError('use a .glb, .dae or .obj file (FBX: convert in Blender or export glTF Binary)')

# ----------------------------------------------------------------------------------------------------- math helpers
def bbox(pos):
    return [min(p[i] for p in pos) for i in range(3)], [max(p[i] for p in pos) for i in range(3)]

def rot_matrix(rx, ry, rz):
    k = math.pi / 180.0
    def rx_(a): c, s = math.cos(a*k), math.sin(a*k); return [[1,0,0],[0,c,-s],[0,s,c]]
    def ry_(a): c, s = math.cos(a*k), math.sin(a*k); return [[c,0,s],[0,1,0],[-s,0,c]]
    def rz_(a): c, s = math.cos(a*k), math.sin(a*k); return [[c,-s,0],[s,c,0],[0,0,1]]
    def mul(a, b): return [[sum(a[i][k_]*b[k_][j] for k_ in range(3)) for j in range(3)] for i in range(3)]
    return mul(rz_(rz), mul(ry_(ry), rx_(rx)))

def apply(M, v): return tuple(sum(M[i][j]*v[j] for j in range(3)) for i in range(3))

def centroid_bias(pos, axis):
    lo, hi = bbox(pos); mid = (lo[axis] + hi[axis]) / 2.0
    return sum(p[axis] for p in pos) / len(pos) - mid

def auto_orient(new, orig):
    """Rotation matrix taking the new model's longest axis onto the original's, pointing the same way."""
    lo, hi = bbox(new.pos); ext = [hi[i]-lo[i] for i in range(3)]; an = ext.index(max(ext))
    lo2, hi2 = bbox(orig.pos); ext2 = [hi2[i]-lo2[i] for i in range(3)]; ao = ext2.index(max(ext2))
    R = [[1,0,0],[0,1,0],[0,0,1]]
    if an != ao:
        third = 3 - an - ao
        # a quarter turn about the third axis moves axis an onto ao (either +ao or -ao)
        angle = 90.0
        e = [0, 0, 0]; e[third] = angle
        cand = rot_matrix(*e)
        v = [0, 0, 0]; v[an] = 1; mapped = apply(cand, v)
        if mapped[ao] < 0: e[third] = -angle; cand = rot_matrix(*e)
        R = cand
    rotated = [apply(R, p) for p in new.pos]
    # same direction: compare which end of the longest axis the mass leans towards
    if centroid_bias(rotated, ao) * centroid_bias(orig.pos, ao) < 0:
        e = [0, 0, 0]; e[(ao + 1) % 3] = 180.0
        R = [[sum(rot_matrix(*e)[i][k]*R[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return R

def transform_mesh(new, orig, opts):
    """Returns a new Mesh in the game's axes and units."""
    R = auto_orient(new, orig) if opts['auto_orient'] else [[1,0,0],[0,1,0],[0,0,1]]
    U = rot_matrix(opts['rx'], opts['ry'], opts['rz'])
    out = Mesh(); out.tris = list(new.tris); out.tri_group = list(new.tri_group)
    pos = [apply(U, apply(R, p)) for p in new.pos]
    nrm = [apply(U, apply(R, n)) for n in new.nrm]
    lo_o, hi_o = bbox(orig.pos); ext_o = [hi_o[i]-lo_o[i] for i in range(3)]; ao = ext_o.index(max(ext_o))
    lo, hi = bbox(pos); ext = [hi[i]-lo[i] for i in range(3)]
    scale = opts['scale']
    if opts['auto_fit']:
        longest = max(ext) or 1.0
        scale *= max(ext_o) / longest
    pos = [tuple(c*scale for c in p) for p in pos]
    lo, hi = bbox(pos)
    shift = [0.0, 0.0, 0.0]
    for i in range(3):
        if opts['align'] == 'start' and i == ao: shift[i] = lo_o[i] - lo[i]
        else: shift[i] = (lo_o[i]+hi_o[i])/2.0 - (lo[i]+hi[i])/2.0
    out.pos = [tuple(p[i]+shift[i] for i in range(3)) + () for p in pos]
    out.nrm = nrm
    out.uv = [(u[0], 1.0 - u[1]) if opts['flip_v'] else u for u in new.uv]
    return out

# ----------------------------------------------------------------------------------------------------------- install
def stem_parts(stem):
    m = re.match(r'^(.*)__(\d+)v$', stem)
    return (m.group(1), int(m.group(2))) if m else (stem, None)

def install(mods_dir, mod_name, stem, mesh, texture_bytes=None, texture_ext='.png'):
    base = os.path.join(mods_dir, mod_name)
    write_obj(mesh, os.path.join(base, 'models', stem + '.obj'), 'installed by mod_tool.py')
    tex = stem_parts(stem)[0]
    wrote_tex = None
    if texture_bytes is not None:
        os.makedirs(os.path.join(base, 'textures'), exist_ok=True)
        wrote_tex = os.path.join(base, 'textures', tex + '.png')
        with open(wrote_tex, 'wb') as f: f.write(texture_bytes)
    return wrote_tex

def to_png_bytes(data, mime, path_hint=''):
    if data[:8] == b'\x89PNG\r\n\x1a\n': return data
    try:
        from PIL import Image
        import io
        im = Image.open(io.BytesIO(data)).convert('RGBA'); out = io.BytesIO(); im.save(out, 'PNG'); return out.getvalue()
    except ImportError:
        raise ValueError('this texture is not a PNG; run  pip install pillow  or convert it to PNG first')

# --------------------------------------------------------------------------------------------------------------- GUI
def run_gui(save_dir):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    try:
        from tkinterdnd2 import TkinterDnD, DND_FILES
        root = TkinterDnD.Tk(); dnd = True
    except Exception:
        root = tk.Tk(); dnd = False
    root.title('Dark Cloud model replacer'); root.geometry('1100x680')
    mods_dir = os.path.join(save_dir, 'mods'); dump_dir = os.path.join(mods_dir, '_dump', 'models')
    project = os.path.dirname(os.path.abspath(__file__))
    state = {'orig': None, 'new': None, 'image': None, 'image_mime': None, 'src': None, 'tex_path': None, 'yaw': 0.6, 'pitch': 0.4, 'zoom': 1.0, 'result': None}

    left = ttk.Frame(root, padding=8); left.pack(side='left', fill='y')
    ttk.Label(left, text='1. What to replace (search):').pack(anchor='w')
    search = tk.StringVar(); ttk.Entry(left, textvariable=search, width=34).pack(fill='x')
    listbox = tk.Listbox(left, width=38, height=28, exportselection=False); listbox.pack(fill='y', expand=True)
    names = []
    def refresh_list(*_):
        listbox.delete(0, 'end'); names.clear()
        q = search.get().lower().strip()
        if os.path.isdir(dump_dir):
            for f in sorted(os.listdir(dump_dir)):
                if f.lower().endswith('.obj') and q in f.lower():
                    stem = f[:-4]; names.append(stem)
                    done = os.path.exists(os.path.join(mods_dir, modname.get(), 'models', stem + '.obj'))
                    listbox.insert('end', ('[x] ' if done else '    ') + stem)
        if not names:
            listbox.insert('end', '(no dumped models yet: use "Dump models" below)')
    search.trace_add('write', refresh_list)
    btns = ttk.Frame(left); btns.pack(fill='x', pady=4)
    def launch(extra):
        ps = os.path.join(project, 'run_win.ps1')
        subprocess.Popen(['powershell', '-ExecutionPolicy', 'Bypass', '-NoExit', '-File', ps] + extra)
    ttk.Button(btns, text='Dump models (starts the game)', command=lambda: launch(['-DumpModels'])).pack(fill='x')
    ttk.Button(btns, text='Refresh list', command=refresh_list).pack(fill='x')

    right = ttk.Frame(root, padding=8); right.pack(side='left', fill='both', expand=True)
    top = ttk.Frame(right); top.pack(fill='x')
    ttk.Label(top, text='2. Your model (.glb, .dae or .obj):').grid(row=0, column=0, sticky='w')
    src_label = ttk.Label(top, text='nothing chosen' + ('  (you can drop a file on this window)' if dnd else ''), width=60); src_label.grid(row=0, column=2, sticky='w')
    ttk.Label(top, text='Mod folder name:').grid(row=1, column=0, sticky='w')
    modname = tk.StringVar(value='mymodels'); ttk.Entry(top, textvariable=modname, width=24).grid(row=1, column=1, sticky='w')
    modname.trace_add('write', refresh_list)

    opt = ttk.LabelFrame(right, text='Fit (the preview updates as you change these)', padding=6); opt.pack(fill='x', pady=6)
    v = {k: tk.DoubleVar(value=val) for k, val in (('scale', 1.0), ('rx', 0.0), ('ry', 0.0), ('rz', 0.0))}
    auto_orient_v = tk.BooleanVar(value=True); auto_fit_v = tk.BooleanVar(value=True); flip_v_v = tk.BooleanVar(value=False)
    align_v = tk.StringVar(value='start'); use_tex = tk.BooleanVar(value=True)
    ttk.Checkbutton(opt, text='Auto-orient (long axis matches the original)', variable=auto_orient_v, command=lambda: update()).grid(row=0, column=0, columnspan=2, sticky='w')
    ttk.Checkbutton(opt, text='Auto-fit size to the original', variable=auto_fit_v, command=lambda: update()).grid(row=0, column=2, columnspan=2, sticky='w')
    ttk.Checkbutton(opt, text='Flip texture vertically', variable=flip_v_v, command=lambda: update()).grid(row=0, column=4, sticky='w')
    for c, (lab, key) in enumerate((('Size x', 'scale'), ('Turn X°', 'rx'), ('Turn Y°', 'ry'), ('Turn Z°', 'rz'))):
        ttk.Label(opt, text=lab).grid(row=1, column=c*2, sticky='e')
        e = ttk.Spinbox(opt, from_=-360 if key != 'scale' else 0.01, to=360 if key != 'scale' else 100, increment=90 if key != 'scale' else 0.1, textvariable=v[key], width=7, command=lambda: update())
        e.grid(row=1, column=c*2+1, sticky='w'); e.bind('<Return>', lambda _e: update()); e.bind('<FocusOut>', lambda _e: update())
    ttk.Label(opt, text='Align').grid(row=2, column=0, sticky='e')
    ttk.Combobox(opt, textvariable=align_v, values=['start', 'center'], width=8, state='readonly').grid(row=2, column=1, sticky='w')
    align_v.trace_add('write', lambda *_: update())
    ttk.Checkbutton(opt, text="Use the model's own texture (if it has one / you picked one)", variable=use_tex).grid(row=2, column=2, columnspan=4, sticky='w')

    canvas = tk.Canvas(right, bg='#1c1c24', height=380); canvas.pack(fill='both', expand=True)
    status = ttk.Label(right, text='Pick a model on the left, then choose your file.'); status.pack(fill='x')

    def opts():
        try: return {'scale': float(v['scale'].get()), 'rx': float(v['rx'].get()), 'ry': float(v['ry'].get()), 'rz': float(v['rz'].get()),
                     'auto_orient': auto_orient_v.get(), 'auto_fit': auto_fit_v.get(), 'flip_v': flip_v_v.get(), 'align': align_v.get()}
        except (tk.TclError, ValueError): return None

    def project_point(p, w, h, scale):
        cy, sy = math.cos(state['yaw']), math.sin(state['yaw']); cp, sp = math.cos(state['pitch']), math.sin(state['pitch'])
        x = p[0]*cy - p[1]*sy; y = p[0]*sy + p[1]*cy; z = p[2]
        y2 = y*cp - z*sp; z2 = y*sp + z*cp
        return (w/2 + x*scale, h/2 - z2*scale)

    def draw(mesh, colour, c, scale, centre, limit=2500):
        w, h = canvas.winfo_width(), canvas.winfo_height()
        step = max(1, len(mesh.tris) // limit)
        for t in mesh.tris[::step]:
            pts = [project_point(tuple(mesh.pos[i][k]-centre[k] for k in range(3)), w, h, scale) for i in t]
            canvas.create_line(pts[0][0], pts[0][1], pts[1][0], pts[1][1], pts[2][0], pts[2][1], pts[0][0], pts[0][1], fill=colour)

    def redraw():
        canvas.delete('all')
        if state['orig'] is None: return
        lo, hi = bbox(state['orig'].pos); centre = [(lo[i]+hi[i])/2 for i in range(3)]
        size = max(hi[i]-lo[i] for i in range(3)) or 1.0
        scale = min(canvas.winfo_width(), canvas.winfo_height()) * 0.8 / size * state['zoom']
        draw(state['orig'], '#6a6a7a', canvas, scale, centre)
        if state['result'] is not None: draw(state['result'], '#ff9a3c', canvas, scale, centre)
        canvas.create_text(8, 8, anchor='nw', fill='#aaaabb', text='grey: original   orange: yours   (drag to rotate, wheel to zoom)')

    def update():
        o = opts()
        if state['orig'] is None or state['new'] is None or o is None:
            state['result'] = None; redraw(); return
        state['result'] = transform_mesh(state['new'], state['orig'], o); redraw()

    def pick_original(*_):
        sel = listbox.curselection()
        if not sel or sel[0] >= len(names): return
        stem = names[sel[0]]
        state['orig'] = load_obj(os.path.join(dump_dir, stem + '.obj')); state['stem'] = stem
        status.config(text='Replacing: %s  (%d vertices)' % (stem, len(state['orig'].pos))); update()
    listbox.bind('<<ListboxSelect>>', pick_original)

    def load_source(path):
        try:
            mesh, image, mime = load_mesh(path)
        except Exception as e:
            messagebox.showerror('Cannot read file', str(e)); return
        state.update(new=mesh, image=image, image_mime=mime, src=path)
        src_label.config(text=os.path.basename(path) + ('  (has a texture)' if image else ''))
        update()
    def choose():
        p = filedialog.askopenfilename(filetypes=[('3D models', '*.glb *.dae *.obj')])
        if p: load_source(p)
    ttk.Button(top, text='Choose file...', command=choose).grid(row=0, column=1, sticky='w')
    def choose_tex():
        p = filedialog.askopenfilename(filetypes=[('Images', '*.png *.jpg *.jpeg')])
        if p:
            state['tex_path'] = p; status.config(text='Texture: ' + os.path.basename(p))
    ttk.Button(top, text='Choose texture (optional)...', command=choose_tex).grid(row=1, column=2, sticky='w')
    if dnd:
        def on_drop(ev):
            files = root.tk.splitlist(ev.data)
            if files: load_source(files[0])
        root.drop_target_register(DND_FILES); root.dnd_bind('<<Drop>>', on_drop)

    def do_install():
        if state['orig'] is None or state['new'] is None:
            messagebox.showinfo('Nothing to install', 'Pick the model to replace and your own file first.'); return
        o = opts()
        if o is None: messagebox.showerror('Bad number', 'Check the size and turn boxes.'); return
        mesh = transform_mesh(state['new'], state['orig'], o)
        tex = None
        try:
            if use_tex.get():
                if state['tex_path']: tex = to_png_bytes(open(state['tex_path'], 'rb').read(), None)
                elif state['image']: tex = to_png_bytes(state['image'], state['image_mime'])
            name = modname.get().strip() or 'mymodels'
            wrote = install(mods_dir, name, state['stem'], mesh, tex)
        except Exception as e:
            messagebox.showerror('Install failed', str(e)); return
        refresh_list()
        status.config(text='Installed into mods\\%s. Start the game to see it.%s' % (name, ' Texture saved too.' if wrote else ' (kept the original texture)'))
    def do_remove():
        if state.get('stem') is None: return
        name = modname.get().strip() or 'mymodels'
        p = os.path.join(mods_dir, name, 'models', state['stem'] + '.obj')
        if os.path.exists(p): os.remove(p); status.config(text='Removed ' + p); refresh_list()
    act = ttk.Frame(right); act.pack(fill='x', pady=4)
    ttk.Button(act, text='3. Install into mod', command=do_install).pack(side='left')
    ttk.Button(act, text='Remove this replacement', command=do_remove).pack(side='left', padx=8)
    ttk.Button(act, text='Start the game', command=lambda: launch([])).pack(side='right')

    last = {}
    def press(e): last['x'], last['y'] = e.x, e.y
    def drag(e):
        state['yaw'] += (e.x - last['x']) * 0.01; state['pitch'] += (e.y - last['y']) * 0.01
        last['x'], last['y'] = e.x, e.y; redraw()
    def wheel(e): state['zoom'] *= 1.1 if e.delta > 0 else 0.9; redraw()
    canvas.bind('<Button-1>', press); canvas.bind('<B1-Motion>', drag); canvas.bind('<MouseWheel>', wheel)
    canvas.bind('<Configure>', lambda e: redraw())
    refresh_list()
    root.mainloop()

if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    save = os.path.join(here, 'win-save')
    if '--save' in sys.argv: save = sys.argv[sys.argv.index('--save') + 1]
    run_gui(save)
