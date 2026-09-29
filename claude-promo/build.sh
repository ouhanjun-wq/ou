#!/usr/bin/env bash
# Build the 30 s film from source: fonts + KaTeX -> soundtrack -> 4K60 frames -> MP4s
#   build/claude-promo-4k.mp4   3840x2160 @ 60 fps  (master)
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
"$FFMPEG" -y -loglevel error -i build/video_2x.mkv -i build/score.wav -map 0:v -map 1:a \
  -vf "scale=1920:1080:flags=lanczos" -c:v libx264 -preset slow -crf 21 -maxrate 14M -bufsize 28M \
  -profile:v high -level 4.2 -pix_fmt yuv420p "${AUDIO[@]}" -movflags +faststart -shortest claude-promo.mp4
"$FFMPEG" -y -loglevel error -ss 29 -i build/video_2x.mkv -frames:v 1 -vf "scale=1920:1080:flags=lanczos" -q:v 3 poster.jpg
"$FFMPEG" -y -loglevel error -i build/score.wav -c:a aac -b:a 256k build/score.m4a   # for the live preview
echo "done -> build/claude-promo-4k.mp4, claude-promo.mp4"
