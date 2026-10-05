# Generates a longsword model + texture for Dark Cloud's Toan knife slot (texture c01w01), units/axes of the dump.
import math, os, sys
from PIL import Image, ImageDraw

W = 128
verts = []   # (pos, normal, uv_image_space)
faces = []

def sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(v):
    l = math.sqrt(sum(c*c for c in v)) or 1.0
    return tuple(c/l for c in v)

def quad(p, uvs, flip=False):
    """p: 4 corners in order; flat normal; outward chosen by `flip`."""
    n = norm(cross(sub(p[1], p[0]), sub(p[2], p[0])))
    if flip: n = tuple(-c for c in n)
    base = len(verts)
    for pos, uv in zip(p, uvs):
        verts.append((pos, n, uv))
    order = [0, 1, 2, 0, 2, 3]
    if flip: order = [0, 2, 1, 0, 3, 2]
    for i in range(0, 6, 3):
        faces.append((base+order[i], base+order[i+1], base+order[i+2]))

def tri(p, uvs, flip=False):
    n = norm(cross(sub(p[1], p[0]), sub(p[2], p[0])))
    if flip: n = tuple(-c for c in n)
    base = len(verts)
    for pos, uv in zip(p, uvs):
        verts.append((pos, n, uv))
    faces.append((base, base+2, base+1) if flip else (base, base+1, base+2))

def rect(q):  # texture quadrant in image px (x0,y0,x1,y1) -> inset uv corners
    x0, y0, x1, y1 = q
    i = 2
    return (x0+i, y0+i, x1-i, y1-i)

BLADE = rect((0, 0, 64, 64)); GUARD = rect((64, 0, 128, 64)); GRIP = rect((0, 64, 64, 128)); POM = rect((64, 64, 128, 128))
def uv(r, a, b):  # a,b in 0..1 across the rect
    return ((r[0] + a*(r[2]-r[0]))/W, (r[1] + b*(r[3]-r[1]))/W)

def box(cx, cy, cz, hx, hy, hz, r):
    x0, x1, y0, y1, z0, z1 = cx-hx, cx+hx, cy-hy, cy+hy, cz-hz, cz+hz
    c = lambda x, y, z: (x, y, z)
    faces6 = [
        ([c(x0,y0,z1), c(x1,y0,z1), c(x1,y1,z1), c(x0,y1,z1)]),  # +z
        ([c(x1,y0,z0), c(x0,y0,z0), c(x0,y1,z0), c(x1,y1,z0)]),  # -z
        ([c(x1,y0,z1), c(x1,y0,z0), c(x1,y1,z0), c(x1,y1,z1)]),  # +x
        ([c(x0,y0,z0), c(x0,y0,z1), c(x0,y1,z1), c(x0,y1,z0)]),  # -x
        ([c(x0,y1,z1), c(x1,y1,z1), c(x1,y1,z0), c(x0,y1,z0)]),  # +y
        ([c(x0,y0,z0), c(x1,y0,z0), c(x1,y0,z1), c(x0,y0,z1)]),  # -y
    ]
    for f in faces6:
        n = norm(cross(sub(f[1], f[0]), sub(f[2], f[0])))
        centre = ((x0+x1)/2, (y0+y1)/2, (z0+z1)/2)
        fc = tuple(sum(p[i] for p in f)/4 for i in range(3))
        outward = sum((fc[i]-centre[i])*n[i] for i in range(3)) > 0
        quad(f, [uv(r,0,0), uv(r,1,0), uv(r,1,1), uv(r,0,1)], flip=not outward)

# ---- blade: rhombic cross-section with a central ridge, tapering to a point -------------------------------------
BW, BT, L0, L1, TIP = 0.62, 0.15, 0.0, 8.2, 9.8
def pt(x, y, z): return (x, y, z)
for sy in (+1, -1):
    for sx in (-1, +1):
        a = pt(sx*BW, 0, L0); b = pt(0, sy*BT, L0); c = pt(0, sy*BT, L1); d = pt(sx*BW, 0, L1)
        n = norm(cross(sub(b, a), sub(c, a)))
        outward = (n[1]*sy > 0) if True else True
        quad([a, b, c, d], [uv(BLADE,0,1), uv(BLADE,1,1), uv(BLADE,1,0.08), uv(BLADE,0,0.08)], flip=not outward)
        tip = pt(0, 0, TIP)
        t = [d, c, tip]
        n = norm(cross(sub(t[1], t[0]), sub(t[2], t[0])))
        outward_t = (n[1]*sy > 0)
        tri(t, [uv(BLADE,0,0.08), uv(BLADE,1,0.08), uv(BLADE,0.5,0)], flip=not outward_t)
# blade base cap hidden by the guard; guard, grip, pommel
box(0, 0, 0.0, 1.55, 0.30, 0.28, GUARD)
box(-1.55, 0, 0.0, 0.0001, 0.0001, 0.0001, GUARD) if False else None
box(1.55, 0.0, 0.0, 0.25, 0.36, 0.34, GUARD)   # guard tips
box(-1.55, 0.0, 0.0, 0.25, 0.36, 0.34, GUARD)
box(0, 0, -1.15, 0.26, 0.26, 0.90, GRIP)
box(0, 0, -2.25, 0.46, 0.40, 0.25, POM)

# ---- OBJ (OBJ convention: v up, so vt_v = 1 - image_v) ---------------------------------------------------------------
os.makedirs('out/models', exist_ok=True); os.makedirs('out/textures', exist_ok=True)
with open('out/models/c01w01__246v.obj', 'w') as f:
    f.write('# custom longsword for the Toan knife slot (texture c01w01)\n')
    for p, n, t in verts: f.write('v %.5f %.5f %.5f\n' % p)
    for p, n, t in verts: f.write('vt %.5f %.5f\n' % (t[0], 1.0 - t[1]))
    for p, n, t in verts: f.write('vn %.5f %.5f %.5f\n' % n)
    for a, b, c in faces:
        f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (a+1,a+1,a+1,b+1,b+1,b+1,c+1,c+1,c+1))
print('verts', len(verts), 'tris', len(faces))

# ---- texture ---------------------------------------------------------------------------------------------------------
img = Image.new('RGBA', (W, W), (0, 0, 0, 255)); d = ImageDraw.Draw(img)
# blade: steel, bright ridge column at x~=right edge of each uv (ridge is u=1), dark edge at u=0
for x in range(0, 64):
    t = x/63.0
    g = int(110 + 120*t**1.3)           # edge dark -> ridge bright
    d.line([(x, 0), (x, 63)], fill=(g, g+6, g+14, 255))
for y in range(0, 64):
    k = 1.0 - 0.18*(y/63.0)
    pass
d.line([(60, 5), (60, 59)], fill=(235, 242, 255, 255)); d.line([(2, 0), (2, 63)], fill=(70, 74, 84, 255))
# guard / pommel: gold
for q, base in (((64, 0, 128, 64), (214, 170, 52)), ((64, 64, 128, 128), (190, 146, 40))):
    for y in range(q[1], q[3]):
        for x in range(q[0], q[2]):
            s = 0.78 + 0.22*math.sin((x-q[0])/9.0)*math.cos((y-q[1])/7.0)
            d.point((x, y), fill=(int(base[0]*s), int(base[1]*s), int(base[2]*s), 255))
    d.rectangle([q[0], q[1], q[2]-1, q[3]-1], outline=(120, 86, 20, 255))
# grip: brown leather wrap
for y in range(64, 128):
    for x in range(0, 64):
        s = 0.7 + 0.3*(((x+y) // 6) % 2)
        d.point((x, y), fill=(int(110*s), int(66*s), int(34*s), 255))
img.save('out/textures/c01w01.png')

# ---- preview (matplotlib, side and top) -----------------------------------------------------------------------------
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import numpy as np
tris = [[verts[i][0] for i in f] for f in faces]
fig = plt.figure(figsize=(9, 4))
for k, (el, az) in enumerate(((20, -60), (90, -90))):
    ax = fig.add_subplot(1, 2, k+1, projection='3d')
    pc = Poly3DCollection(tris, facecolor=(0.7, 0.75, 0.85), edgecolor=(0.2, 0.2, 0.3), linewidths=0.3)
    ax.add_collection3d(pc)
    ax.set_xlim(-3, 3); ax.set_ylim(-3, 3); ax.set_zlim(-3, 10); ax.set_box_aspect((6, 6, 13)); ax.view_init(el, az)
plt.savefig('preview.png', dpi=80)
