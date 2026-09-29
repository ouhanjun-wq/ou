#!/usr/bin/env bash
# Build the 30 s film from source: fonts + KaTeX -> soundtrack -> 4K60 frames -> MP4s
#   build/claude-promo-4k.mp4   3840x2160 @ 60 fps  (master)
#   build/claude-promo-4k-hevc.mp4  3840x2160 @ 60 fps, H.265 (~27 MB, for sharing)
#   claude-promo.mp4            1920x1080 @ 60 fps  (committed web copy)
set -euo pipefail
cd "$(dirname "$0")"

python3 -m pip install -q numpy scipy imageio-ffmpeg
FFMPEG=$(python3 -c "import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())")

# assets (not committed: ~22 MB of web fonts, KaTeX)
[ -f assets/fonts.css ] || python3 tools/fetch_fonts.py
if [ ! -f assets/katex/katex.min.js ]; then
  npm install --silent --no-save --prefix build/npm katex@0.16.47
  mkdir -p assets/katex
  cp -r build/npm/node_modules/katex/dist/{katex.min.css,katex.min.js,fonts} assets/katex/
fi

python3 tools/score.py                                                   # -> build/score.wav
node tools/render.mjs --video --scale 2 --jpeg --workers "${WORKERS:-4}" # -> build/video_2x.mkv (resumable)

AUDIO=(-c:a aac -b:a 256k -af "volume=-1.5dB,alimiter=limit=0.84:level=false")
"$FFMPEG" -y -loglevel error -i build/video_2x.mkv -i build/score.wav -map 0:v -map 1:a \
  -c:v libx264 -preset slow -crf 20 -maxrate 45M -bufsize 90M -profile:v high -level 5.2 -pix_fmt yuv420p \
  "${AUDIO[@]}" -movflags +faststart -shortest build/claude-promo-4k.mp4
# 1080p60 web copy: two-pass to ~7 Mbps so it stays under 30 MiB
V=(-vf "scale=1920:1080:flags=lanczos" -c:v libx264 -preset slow -b:v 6800k -maxrate 12M -bufsize 24M
   -passlogfile build/p1080 -profile:v high -level 4.2 -pix_fmt yuv420p)
"$FFMPEG" -y -loglevel error -i build/video_2x.mkv "${V[@]}" -pass 1 -an -f null /dev/null
"$FFMPEG" -y -loglevel error -i build/video_2x.mkv -i build/score.wav -map 0:v -map 1:a "${V[@]}" -pass 2 \
  -c:a aac -b:a 192k -af "volume=-1.5dB,alimiter=limit=0.84:level=false" -movflags +faststart -shortest claude-promo.mp4
# 4K60 HEVC share copy (~27 MB)
X=(-c:v libx265 -preset medium -b:v 7300k -tag:v hvc1 -pix_fmt yuv420p -vf hqdn3d=1:1:3:3)
"$FFMPEG" -y -loglevel error -i build/video_2x.mkv "${X[@]}" \
  -x265-params "pass=1:stats=build/p4k.log:vbv-maxrate=11000:vbv-bufsize=22000:log-level=error" -an -f null /dev/null
"$FFMPEG" -y -loglevel error -i build/video_2x.mkv -i build/score.wav -map 0:v -map 1:a "${X[@]}" \
  -x265-params "pass=2:stats=build/p4k.log:vbv-maxrate=11000:vbv-bufsize=22000:log-level=error" \
  -c:a aac -b:a 192k -af "volume=-1.5dB,alimiter=limit=0.84:level=false" -movflags +faststart -shortest build/claude-promo-4k-hevc.mp4
"$FFMPEG" -y -loglevel error -ss 29 -i build/video_2x.mkv -frames:v 1 -vf "scale=1920:1080:flags=lanczos" -q:v 3 poster.jpg
"$FFMPEG" -y -loglevel error -i build/score.wav -c:a aac -b:a 256k build/score.m4a   # for the live preview
echo "done -> build/claude-promo-4k.mp4, build/claude-promo-4k-hevc.mp4, claude-promo.mp4"
