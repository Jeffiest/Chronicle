#!/usr/bin/env python3
"""Build the high-res texture pack + runtime index from assets_upscaled/.

The port's texture hook (port/src/platform/mods.cpp, Dump()) identifies a texture by
its DECODED level-0 RGBA (indexed textures expanded through their palette).  This tool
turns that identity into a stable key and groups every manifest member that decodes to
the same picture under one pack file.

Key    = md5( width:u32le || height:u32le || raw level-0 RGBA bytes )
Sources: original-disc-data/<root>/<rel>, or the deswizzled copy when the asset was
exported from deswizzled; indexed members are expanded through their palette.

Steps:
  1. runtime keys for every manifest member
  2. language dedup (same container with the language token swapped); merge only when
     the original *and* the upscaled pictures differ by noise, keeping the English/usa file
  3. copy the kept upscaled PNGs to hirespack/tex/<ab>/<key>.png and write index.json
  4. checks -> hirespack/REPORT.md

usage: python build_hirespack.py [--workers N]

Writes only under hirespack/ (plus this script).  Never moves or deletes anything.
"""

import argparse
import csv
import hashlib
import json
import os
import struct
import sys
import time
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)
from match_tool import normalize  # noqa: E402

MANIFEST = os.path.join(ROOT, "assets_manifest.json")
SRC_ROOTS = {
    "rgba": os.path.join(ROOT, "original-disc-data", "rgba"),
    "indexed": os.path.join(ROOT, "original-disc-data", "indexed"),
}
DESW = os.path.join(ROOT, "deswizzled", "original-disc-data")
ASSETS = os.path.join(ROOT, "assets")
UP = os.path.join(ROOT, "assets_upscaled")
UP_SUB = os.path.join(UP, "text_and_icons")
OUT = os.path.join(ROOT, "hirespack")
TEX = os.path.join(OUT, "tex")
REVIEW = os.path.join(OUT, "review")

MEAN_T = 2.0  # mean abs RGB diff below this is "noise"
MAX_T = 24  # and no visible pixel may differ by more than this
BORDER_LO, BORDER_HI = 2.0, 8.0  # contact-sheet range for borderline groups
LANG_RANK = {"en": 0, "us": 0, "us_e": 1, "uk": 1}  # English/usa preferred
VIS_ALPHA = 16  # RGB under this alpha is arbitrary; ignore it, like check_upscaled.py


# ----------------------------------------------------------------------------- step 1
def member_path(root, rel, exported):
    """Resolve a manifest member to the RGBA PNG the key is computed from."""
    if exported == "deswizzled":
        p = os.path.join(DESW, root, rel)
        if os.path.exists(p):
            return p
    p = os.path.join(ROOT, "original-disc-data", root, rel)
    return p if os.path.exists(p) else None


def key_worker(task):
    aid, midx, root, rel, exported = task
    p = member_path(root, rel, exported)
    if p is None:
        return (aid, midx, None, 0, 0, "no source file")
    try:
        im = Image.open(p).convert("RGBA")  # expands indexed palettes to RGBA
        w, h = im.size
        raw = np.asarray(im)
        md5 = hashlib.md5()
        md5.update(struct.pack("<II", w, h))
        md5.update(raw.tobytes())
        return (aid, midx, md5.hexdigest(), w, h, None)
    except Exception as e:  # pragma: no cover - reported, never guessed
        return (aid, midx, None, 0, 0, str(e)[:160])


# ----------------------------------------------------------------------------- step 2
def vis_diff(a, b):
    """(mean, max) abs RGB diff over pixels visible in either image, or None on size mismatch."""
    if a.shape != b.shape:
        return None
    vis = (a[..., 3] > VIS_ALPHA) | (b[..., 3] > VIS_ALPHA)
    if not vis.any():
        return (0.0, 0)
    d = np.abs(a[..., :3].astype(np.int16) - b[..., :3].astype(np.int16))
    dd = d[vis]
    return (float(dd.mean()), int(dd.max()))


def upscaled_path(a):
    p = os.path.join(UP, a["file"])
    if os.path.exists(p):
        return p
    p = os.path.join(UP_SUB, a["file"])
    return p if os.path.exists(p) else None


def lang_rank(lang):
    return LANG_RANK.get(lang, 2 if lang else 3)


def _load(a, folder):
    im = Image.open(os.path.join(folder, a["file"])).convert("RGBA")
    return np.asarray(im)


def write_contact_sheet(gi, nk, cand, eff, index):
    """Horizontal montage of each variant's upscaled picture, rep marked."""
    H = 200
    tiles = []
    for a in cand:
        img = Image.open(upscaled_path(a)).convert("RGBA")
        scale = H / max(1, img.height)
        img = img.resize((max(1, int(img.width * scale)), H), Image.LANCZOS)
        tiles.append(
            (
                img,
                f"{a['id']} {a['lang'] or ''}{' [rep]' if a['id'] == eff[a['id']][1] else ''}",
            )
        )
    gap, label_h = 8, 16
    total_w = sum(t.width for t, _ in tiles) + gap * (len(tiles) + 1)
    sheet = Image.new("RGBA", (total_w, H + label_h + gap * 2), (24, 24, 28, 255))
    draw = ImageDraw.Draw(sheet)
    x = gap
    for img, label in tiles:
        sheet.alpha_composite(img, (x, gap))
        draw.text((x, gap + H + 2), label, fill=(230, 230, 230, 255))
        x += img.width + gap
    safe = "".join(c if c.isalnum() else "_" for c in nk)[:70].strip("_")
    name = f"g{gi:03d}_{safe}.png"
    sheet.convert("RGB").save(os.path.join(REVIEW, name))
    index[name] = nk


def language_dedup(assets, member_keys, log):
    """Assign every usable asset an effective upscaled file.  Returns (eff, rows, sheet_index)."""
    groups = defaultdict(list)
    for a in assets:
        nk, _lang = normalize(a["rep"]["rel"])
        groups[nk].append(a)

    eff = {}  # aid -> (eff path, rep aid)
    for a in assets:
        p = upscaled_path(a)
        if p is not None and member_keys.get((a["id"], 0)):
            eff[a["id"]] = (p, a["id"])

    rows, sheet_index = [], {}
    n_merge_components = merged_assets = 0
    gi = 0
    for nk in sorted(groups):
        g = groups[nk]
        langs = sorted({a["lang"] for a in g if a["lang"]})
        if len(langs) < 2:
            continue
        gi += 1
        cand = [a for a in g if a["id"] in eff]
        if len(cand) < 2:
            rows.append(
                [nk, ";".join(f"{a['id']}({a['lang']})" for a in g), "single", ""]
            )
            continue

        orig = {a["id"]: _load(a, ASSETS) for a in cand}
        parent = {a["id"]: a["id"] for a in cand}

        def find(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        def union(x, y):
            rx, ry = find(x), find(y)
            if rx != ry:
                parent[ry] = rx

        pair_means = []
        up_cache = {}
        for i in range(len(cand)):
            for j in range(i + 1, len(cand)):
                A, B = cand[i], cand[j]
                od = vis_diff(orig[A["id"]], orig[B["id"]])
                if od is None:
                    continue
                pair_means.append(od[0])
                if (
                    od[0] < MEAN_T and od[1] <= MAX_T
                ):  # cheap check first: only then the big upscaled
                    for a in (A, B):
                        if a["id"] not in up_cache:
                            up_cache[a["id"]] = _load(
                                a, os.path.dirname(eff[a["id"]][0])
                            )
                    ud = vis_diff(up_cache[A["id"]], up_cache[B["id"]])
                    if ud is not None and ud[0] < MEAN_T and ud[1] <= MAX_T:
                        union(A["id"], B["id"])
        del orig, up_cache

        comps = defaultdict(list)
        for a in cand:
            comps[find(a["id"])].append(a)
        merged_here = False
        for members in comps.values():
            members.sort(key=lambda a: (lang_rank(a["lang"]), a["id"]))
            rep = members[0]
            if len(members) > 1:
                merged_here = True
                n_merge_components += 1
                merged_assets += len(members) - 1
                for m in members:
                    eff[m["id"]] = (eff[rep["id"]][0], rep["id"])

        group_mean = round(sum(pair_means) / len(pair_means), 3) if pair_means else ""
        borderline = (
            bool(pair_means)
            and BORDER_LO <= sum(pair_means) / len(pair_means) < BORDER_HI
        )
        members_str = ";".join(
            f"{a['id']}({a['lang']}){'*' if a['id'] == eff[a['id']][1] else ''}"
            for a in cand
        )
        rows.append(
            [nk, members_str, "merged" if merged_here else "separate", group_mean]
        )
        if merged_here or borderline:
            write_contact_sheet(gi, nk, cand, eff, sheet_index)

    log["lang_groups"] = gi
    log["merge_components"] = n_merge_components
    log["merged_assets"] = merged_assets
    return eff, rows, sheet_index


# ----------------------------------------------------------------------------- step 3
def save_tex_worker(task):
    src, dst = task
    try:
        im = Image.open(src).convert("RGBA")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        im.save(dst, format="PNG", optimize=False)
        return (dst, os.path.getsize(dst), None)
    except Exception as e:  # pragma: no cover
        return (dst, 0, str(e)[:160])


# ----------------------------------------------------------------------------- step 4
def check_worker(task):
    name, folder = task
    import check_upscaled as cv

    cv.UP = folder
    cv.SRC = ASSETS
    return cv.one(name)


# ----------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workers", type=int, default=min(8, os.cpu_count() or 4))
    args = ap.parse_args()
    workers = max(1, args.workers)

    os.makedirs(TEX, exist_ok=True)
    os.makedirs(REVIEW, exist_ok=True)
    log = {}
    t0 = time.time()

    man = json.load(open(MANIFEST, encoding="utf-8"))
    assets = man["assets"]
    log["total_assets"] = len(assets)
    print(f"assets: {len(assets)}", flush=True)

    # ---- 1. runtime keys for every manifest member ----------------------------------
    tasks = []
    for a in assets:
        for mi, m in enumerate(a["members"]):
            tasks.append((a["id"], mi, m["root"], m["rel"], a["exported_from"]))
    print(f"members: {len(tasks)} -> computing keys", flush=True)
    member_keys, uncomputable = {}, []
    with ProcessPoolExecutor(max_workers=workers) as ex:
        for aid, midx, key, w, h, err in ex.map(key_worker, tasks, chunksize=32):
            if key is None:
                uncomputable.append({"asset": aid, "member": midx, "reason": err})
            else:
                member_keys[(aid, midx)] = key
    print(
        f"keys: {len(member_keys)} ok, {len(uncomputable)} uncomputable "
        f"({round(time.time() - t0, 1)}s)",
        flush=True,
    )

    # ---- 2. language dedup ----------------------------------------------------------
    eff, rows, sheet_index = language_dedup(assets, member_keys, log)
    with open(
        os.path.join(OUT, "language_dedup.csv"), "w", newline="", encoding="utf-8"
    ) as f:
        wr = csv.writer(f)
        wr.writerow(["group", "members", "decision", "mean diff"])
        wr.writerows(rows)
    print(
        f"language: {log.get('lang_groups', 0)} groups, "
        f"{log.get('merge_components', 0)} merged components, "
        f"{len(sheet_index)} contact sheets",
        flush=True,
    )

    # ---- 3. pack --------------------------------------------------------------------
    # effective file -> the representative asset that names it
    file_rep = {}
    for a in assets:
        if a["id"] in eff:
            p, rep_id = eff[a["id"]]
            file_rep.setdefault(p, rep_id)
    by_id = {a["id"]: a for a in assets}

    fileinfo, odd, no_repkey, dup_key = {}, [], [], []
    seen_key = {}
    for p in sorted(file_rep):
        rep = by_id[file_rep[p]]
        rk = member_keys.get((rep["id"], 0))
        if not rk:
            no_repkey.append({"file": p, "rep": rep["id"]})
            continue
        uw, uh = Image.open(p).size
        ow, oh = rep["w"], rep["h"]
        sx, sy = uw / ow, uh / oh
        if not (abs(sx - sy) < 1e-9 and sx >= 1 and abs(sx - round(sx)) < 1e-9):
            odd.append(
                {
                    "file": p,
                    "rep": rep["id"],
                    "orig": [ow, oh],
                    "up": [uw, uh],
                    "scale": [sx, sy],
                }
            )
            continue
        if rk in seen_key:  # two files decode to one representative texture: keep one
            dup_key.append({"key": rk, "kept": seen_key[rk], "dropped": p})
            continue
        seen_key[rk] = p
        fileinfo[p] = {"rep": rep["id"], "key": rk, "scale": int(round(sx))}
    print(
        f"files: {len(file_rep)} unique, {len(fileinfo)} usable, {len(odd)} odd scale, "
        f"{len(dup_key)} duplicate rep-keys",
        flush=True,
    )

    # index: every member key of every kept asset -> the one kept file for that texture
    textures, conflicts, uncomputable_kept = {}, {}, 0
    member_pairs = 0
    for a in sorted(assets, key=lambda x: x["id"]):
        if a["id"] not in eff:
            continue
        info = fileinfo.get(eff[a["id"]][0])
        if info is None:
            continue
        relfile = "tex/%s/%s.png" % (info["key"][:2], info["key"])
        for mi in range(len(a["members"])):
            k = member_keys.get((a["id"], mi))
            if not k:
                uncomputable_kept += 1
                continue
            # A grouped manifest asset can contain distinct decoded pictures.
            # Its representative upscale is valid only for the representative key.
            if k != info["key"]:
                continue
            member_pairs += 1
            entry = {"file": relfile, "w": a["w"], "h": a["h"], "scale": info["scale"]}
            prev = textures.get(k)
            if prev is None:
                textures[k] = entry
            elif prev["file"] != relfile:
                conflicts[(k, prev["file"], relfile)] = True
    index = {
        "version": 1,
        "scale": "per-texture",
        "textures": {k: textures[k] for k in sorted(textures)},
    }
    with open(os.path.join(OUT, "index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, indent=1)

    # copy only the files the index actually points at (no unreferenced leftovers)
    referenced = {v["file"] for v in textures.values()}
    dst_of, copy_tasks = {}, []
    for p, info in fileinfo.items():
        relfile = "tex/%s/%s.png" % (info["key"][:2], info["key"])
        if relfile in referenced:
            dst = os.path.join(OUT, relfile.replace("/", os.sep))
            dst_of[p] = dst
            copy_tasks.append((p, dst))
    pack_bytes = 0
    with ProcessPoolExecutor(max_workers=workers) as ex:
        for dst, size, err in ex.map(save_tex_worker, copy_tasks, chunksize=16):
            pack_bytes += size
    print(
        f"copied {len(copy_tasks)} files, {round(pack_bytes / 1e6, 1)} MB; "
        f"index: {len(textures)} keys from {member_pairs} member pairs, "
        f"{len(conflicts)} conflicts",
        flush=True,
    )

    # bytes saved: own upscaled files of assets merged away into a kept file
    saved = 0
    for a in assets:
        if a["id"] in eff:
            p, rep_id = eff[a["id"]]
            if rep_id != a["id"] and p in dst_of:
                saved += os.path.getsize(upscaled_path(a))

    # ---- 4. checks ------------------------------------------------------------------
    print("running check_upscaled.one() on every kept PNG...", flush=True)
    check_tasks = []
    for p in sorted(dst_of):
        rep = by_id[file_rep[p]]
        check_tasks.append((rep["file"], os.path.dirname(upscaled_path(rep))))
    status = defaultdict(int)
    with ProcessPoolExecutor(max_workers=workers) as ex:
        for r in ex.map(check_worker, check_tasks, chunksize=16):
            status[r.get("status", "error")] += 1

    # ---- report ---------------------------------------------------------------------
    no_up = [a["id"] for a in assets if upscaled_path(a) is None]
    kept_assets = sum(1 for a in assets if a["id"] in eff and eff[a["id"]][0] in dst_of)
    scale_hist = defaultdict(int)
    for info in fileinfo.values():
        scale_hist[info["scale"]] += 1
    conflict_list = sorted(conflicts)
    lines = [
        "# hirespack build report",
        "",
        f"generated: {time.strftime('%Y-%m-%d %H:%M:%S')}  ({round(time.time() - t0, 1)}s, {workers} workers)",
        "",
        "## counts",
        "",
        f"- manifest assets: {log['total_assets']}",
        f"- assets with an upscaled PNG: {sum(1 for a in assets if upscaled_path(a) is not None)}",
        f"- assets kept in the pack: {kept_assets}",
        f"- runtime keys in index.json: {len(textures)}",
        f"- member keys mapped (asset, member) pairs: {member_pairs}",
        f"- pack files kept: {len(dst_of)}",
        f"- total pack size: {pack_bytes} bytes ({round(pack_bytes / 1e6, 2)} MB)",
        f"- bytes saved by language dedup: {saved} ({round(saved / 1e6, 2)} MB)",
        f"- language groups examined: {log.get('lang_groups', 0)}; "
        f"merged components: {log.get('merge_components', 0)}; merged-away assets: {log.get('merged_assets', 0)}",
        f"- contact sheets in review/: {len(sheet_index)}",
        "",
        "## check_upscaled.one() status (kept PNGs)",
        "",
    ]
    for s in sorted(status):
        lines.append(f"- {s}: {status[s]}")
    lines += [
        "",
        "## excluded",
        "",
        f"- assets with no upscaled PNG: {len(no_up)}",
    ]
    if no_up:
        lines.append(f"  - first 20: {', '.join(no_up[:20])}")
    lines.append(
        f"- files with no representative key (cannot be named): {len(no_repkey)}"
    )
    for n in no_repkey[:40]:
        lines.append(f"  - {os.path.relpath(n['file'], ROOT)} rep={n['rep']}")
    lines.append(f"- files with odd scale (non-integer or x!=y): {len(odd)}")
    for o in odd[:40]:
        lines.append(
            f"  - {os.path.relpath(o['file'], ROOT)} rep={o['rep']} "
            f"orig={o['orig']} up={o['up']} scale={o['scale']}"
        )
    lines.append(f"- files dropped as duplicate representative keys: {len(dup_key)}")
    for d in dup_key[:40]:
        lines.append(
            f"  - key {d['key'][:12]} kept={os.path.relpath(d['kept'], ROOT)} "
            f"dropped={os.path.relpath(d['dropped'], ROOT)}"
        )
    lines += [
        f"- manifest members with no computable key: {len(uncomputable)}",
    ]
    for u in uncomputable[:40]:
        lines.append(f"  - {u['asset']} member {u['member']}: {u['reason']}")
    lines += [
        f"- member keys of kept assets with no computable key: {uncomputable_kept}",
        f"- runtime-key conflicts (same key, already mapped to another file): {len(conflict_list)}",
    ]
    for c in conflict_list[:40]:
        lines.append(f"  - {c[0][:12]} kept={c[1]} dropped={c[2]}")
    lines += [
        "",
        "## notes",
        "",
        "- check_upscaled.one() was run on the representative upscaled source of each kept PNG",
        "  (the pack PNG is the same pixels re-encoded RGBA 8-bit, optimize off).",
        "- index.json top-level `scale` is `per-texture`; each texture entry carries its own",
        "  integer `scale` factor (up w / orig w). Observed factors: "
        + ", ".join(f"{k}x={v}" for k, v in sorted(scale_hist.items()))
        + ".",
        "- RGB pixels under alpha 16 are treated as arbitrary (ignored) in the language diff,",
        "  matching check_upscaled.py; without that, alpha-0 padding shows up as fake differences.",
        "",
    ]
    with open(os.path.join(OUT, "REPORT.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    with open(os.path.join(REVIEW, "contact_sheets.json"), "w", encoding="utf-8") as f:
        json.dump(sheet_index, f, indent=1)

    print("by_status:", dict(status))
    print(f"done in {round(time.time() - t0, 1)}s -> {OUT}")


if __name__ == "__main__":
    main()
