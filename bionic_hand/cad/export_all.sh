#!/usr/bin/env bash
# Export every printed part of the bionic hand to STL.
#   ./export_all.sh          -> stl/
#   ./export_all.sh out      -> out/   (CI: fails on any OpenSCAD warning)
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-stl}"
mkdir -p "$OUT"
scad() { # <output> <file> [-D ...]
  local out="$1" file="$2"; shift 2
  openscad --hardwarnings -o "$OUT/$out.stl" "$@" "$file"
  test -s "$OUT/$out.stl" || { echo "empty: $out"; exit 1; }
}
for f in index middle ring pinky thumb; do
  for p in proximal distal link; do scad "${f}_$p" finger.scad -D "finger=\"$f\"" -D "part=\"$p\""; done
  [ "$f" = thumb ] || scad "${f}_rod" finger.scad -D "finger=\"$f\"" -D 'part="rod"'
done
for p in palm cover_dorsal cover_palmar thenar_cover thumb_bracket; do scad "$p" palm.scad -D "part=\"$p\""; done
scad glove_plate glove.scad -D 'part="plate"'
scad glove_crank glove.scad -D 'part="crank"'
scad glove_link glove.scad -D 'part="link"'
for s in S:17 M:19 L:21; do scad "glove_ring_${s%%:*}" glove.scad -D 'part="ring"' -D "ring_d=${s##*:}"; done
scad glove_thumb_ring glove.scad -D 'part="thumb_ring"'
ls -l "$OUT"
