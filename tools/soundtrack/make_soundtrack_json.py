#!/usr/bin/env python3
"""Write the soundtrack.json the port reads, from docs/soundtrack/matches.json and your own copy of the soundtrack.

    python tools/soundtrack/make_soundtrack_json.py --music-dir "D:/Music/Dark Cloud OST" --out "<save folder>/soundtrack/soundtrack.json"

matches.json says which soundtrack track goes with each game sequence. This script finds the file for each track in
--music-dir (a .flac, .ogg or .wav whose name contains the track's title, Japanese or English), and writes one entry per
sequence. Tracks it cannot find are listed and left out, so those sequences keep their PS2 sound. Nothing is downloaded and
no audio is read. Add --only-ear to leave out matches nobody has listened to. See docs/SOUNDTRACK.md.
"""
import argparse, json, os, sys, unicodedata

EXTENSIONS = ('.flac', '.ogg', '.wav')


def norm(text):
    return unicodedata.normalize('NFKC', text).casefold()


def find_file(files, track):
    """The file whose name starts with the track's title (Japanese) or contains its English name."""
    keys = [norm(track['track'])]
    if track.get('english'):
        keys.append(norm(track['english']))
    for key in keys:
        starts = [f for f in files if norm(os.path.splitext(f)[0]).startswith(key)]
        if len(starts) == 1:
            return starts[0]
        if len(starts) > 1:  # "Broken Promise" also begins "Broken Promise (Orgel Version)": take the shortest name
            return min(starts, key=len)
    for key in keys:
        inside = [f for f in files if key in norm(f)]
        if len(inside) == 1:
            return inside[0]
    return None


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--music-dir', required=True, help='folder holding your copy of the soundtrack')
    ap.add_argument('--out', required=True, help='soundtrack.json to write, normally <save folder>/soundtrack/soundtrack.json')
    ap.add_argument('--matches', default=os.path.join(here, '..', '..', 'docs', 'soundtrack', 'matches.json'))
    ap.add_argument('--only-ear', action='store_true', help='leave out matches picked by audio comparison alone')
    args = ap.parse_args()

    matches = json.load(open(args.matches, encoding='utf-8'))['tracks']
    files = sorted(f for f in os.listdir(args.music_dir) if f.lower().endswith(EXTENSIONS))
    tracks, missing = {}, set()
    for seq, m in matches.items():
        if m['track'] is None or (args.only_ear and m['matched_by'] != 'ear'):
            continue
        name = find_file(files, m)
        if name is None:
            missing.add(m['track'])
            continue
        entry = {'file': os.path.abspath(os.path.join(args.music_dir, name)).replace('\\', '/'), 'gain': 1.0, 'loop': True,
                 'reference': f"matched by {m['matched_by']}: {m['english']}"}
        if m.get('start'):
            entry['start'] = m['start']
        tracks[seq] = entry
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    json.dump({'tracks': tracks}, open(args.out, 'w', encoding='utf-8'), indent=2, ensure_ascii=False)
    print(f'wrote {len(tracks)} entries to {args.out}')
    if missing:
        print('no file found for: ' + ', '.join(sorted(missing)), file=sys.stderr)


if __name__ == '__main__':
    main()
