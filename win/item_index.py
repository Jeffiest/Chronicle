#!/usr/bin/env python3
"""Lists every item with the game file that holds its model and texture, so a texture mod can find the right file.

  python item_index.py [--src <Chronicle source>] [--out <folder>] [--dump <mods\\_dump\\textures.csv>]

Writes <out>/item_index.csv: id, item name, kind, game file (without extension), and, when a texture dump is given,
the texture names that file contains with the exact mod path to put a replacement PNG at.
"""
import argparse, csv, re, sys
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('--src', default=str(Path(__file__).resolve().parents[1]))
ap.add_argument('--out', default='.')
ap.add_argument('--dump', default='')
a = ap.parse_args()
src = Path(a.src)

enum_text = (src / 'ps2/include/itemdata.hpp').read_text(encoding='utf-8', errors='replace')
m = re.search(r'enum Item\s*\{(.*?)\};', enum_text, re.S)
names = {}          # id -> first listed name
values = {}         # name -> id
for name, num in re.findall(r'^\s*(ITEM_[A-Z0-9_]+)\s*=\s*(\d+)\s*,', m.group(1), re.M):
    values[name] = int(num)
    marker = re.search(r'_(START|END)(_|$)|_SLOT_EMPTY$', name) is not None
    if int(num) not in names or (names[int(num)][1] and not marker):
        names[int(num)] = (name, marker)
names = {k: v[0] for k, v in names.items()}

def pretty(n):
    return n.replace('ITEM_', '').replace('_', ' ').title()

btitem = (src / 'ps2/src/btitem.cpp').read_text(encoding='utf-8', errors='replace')
t = re.search(r'char \*ITEM_NAME_TBL_NEW\[\]\s*=\s*\{(.*?)\};', btitem, re.S)
entries = re.findall(r'"([^"]*)"|(no_item_name)|(NULL)', t.group(1))
start = values['ITEM_ATTACH_START']
rows = []
for i, e in enumerate(entries):
    if not e[0]:
        continue
    item = start + i
    rows.append((item, pretty(names.get(item, f'ITEM_{item}')), 'item', 'dun/item/main_data/' + e[0]))

prefix = ['c01w', 'c04w', 'c06w', 'c05w', 'c10w', 'c18w']
chara = ['Toan', 'Xiao', 'Goro', 'Ruby', 'Ungaga', 'Osmond']
first = [values[k] for k in ('ITEM_WEAPON_DAGGER_BROKEN', 'ITEM_WEAPON_WOODENSLINGSHOT_BROKEN', 'ITEM_WEAPON_MALLET_BROKEN',
                             'ITEM_WEAPON_GOLD_RING_BROKEN', 'ITEM_WEAPON_FIGHTING_STICK_BROKEN', 'ITEM_WEAPON_MACHINE_GUN_BROKEN')]
for item in sorted(i for i in names if i >= values['ITEM_WEAPON_START']):
    c = max(k for k in range(6) if item >= first[k])
    off = item - first[c]
    rows.append((item, pretty(names[item]), f'weapon ({chara[c]})', f'dun/item/main_wep/{prefix[c]}{off:02d}'))

textures = {}   # texture name (lowercase) -> size
if a.dump and Path(a.dump).exists():
    for r in csv.reader(open(a.dump, encoding='utf-8', errors='replace')):
        if len(r) >= 3:
            textures.setdefault(r[1].lower(), r[2])

out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
with open(out / 'item_index.csv', 'w', newline='', encoding='utf-8') as f:
    w = csv.writer(f)
    w.writerow(['id', 'item', 'kind', 'texture_name', 'seen_in_dump', 'size', 'save_your_png_as'])
    for item, name, kind, base in rows:
        tex = base.rsplit('/', 1)[-1]
        size = textures.get(tex.lower(), '')
        w.writerow([item, name, kind, tex, 'yes' if size else 'no - play to where it appears with -DumpTextures', size, f'mods/<your mod>/textures/{tex}.png'])
print(f'wrote {out / "item_index.csv"}: {len(rows)} items, {sum(1 for r in rows if r[3].rsplit("/",1)[-1].lower() in textures)} seen in the dump')
