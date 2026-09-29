#!/usr/bin/env bash
# Build the 30 s film from source: fonts + KaTeX -> soundtrack -> frames -> claude-promo.mp4
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

python3 tools/score.py                                        # -> build/score.wav
node tools/render.mjs --video --workers "${WORKERS:-4}"       # -> build/video.mkv (lossless-ish, no audio)

"$FFMPEG" -y -loglevel error -i build/video.mkv -i build/score.wav \
  -map 0:v -map 1:a -c:v libx264 -preset slow -crf "${CRF:-20}" -pix_fmt yuv420p -profile:v high \
  -c:a aac -b:a 256k -af "volume=-1.5dB,alimiter=limit=0.84:level=false" \
  -movflags +faststart -shortest claude-promo.mp4
"$FFMPEG" -y -loglevel error -i build/score.wav -c:a aac -b:a 256k build/score.m4a   # for the live preview
echo "done -> $(pwd)/claude-promo.mp4"
