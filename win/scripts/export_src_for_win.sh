#!/usr/bin/env bash
# Copy upstream port SOURCE (no data, no build dirs, no .git) from WSL to the Claude folder for the Windows-native feasibility study.
set -e
DEST=/mnt/e/Chronicle-project-Claude/Chronicle-src
mkdir -p "$DEST"
cd ~/Chronicle
rsync -a --delete --exclude 'win/' --exclude 'UPSTREAM_COMMIT.txt' --exclude '.git/' --exclude 'data/' --exclude 'data-pal/' --exclude 'save/' --exclude 'build*/' --exclude 'port/build*/' --exclude '*.iso' ./ "$DEST/"
git rev-parse HEAD > "$DEST/UPSTREAM_COMMIT.txt"; git log -1 --format=%cd >> "$DEST/UPSTREAM_COMMIT.txt"
echo "copied: $(find "$DEST" -type f | wc -l) files, $(du -sh "$DEST" | cut -f1)"
cat "$DEST/UPSTREAM_COMMIT.txt"
