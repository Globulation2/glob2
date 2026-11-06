#!/usr/bin/env bash
# motion.sh BINARY OUTDIR [ZOOM] : Honeycomb, legacy GL, HD art, zoom cycle x1..x4 while panning.
set -eo pipefail
dir="$(cd "$(dirname "$0")" && pwd)"
binary="$1" out="$2" zoom="${3:-1.25}"
mkdir -p "$out/profile"
printf 'highResolutionArtwork=1\nuiScale=0\n' > "$out/profile/preferences.txt"
env GLOB2_USER_DATA_DIR="$out/profile" GLOB2_USER_DIR="$out/profile" \
 GLOB2_BENCH_GAME="$dir/honeycomb.game.gz" "GLOB2_BENCH_CAMERA_${CAMERA:-MOTION}=1" \
 GLOB2_BENCH_FLAT=1 GLOB2_BENCH_MODE='2D no clouds' GLOB2_BENCH_FRAMES=360 \
 GLOB2_BENCH_WARMUP_FRAMES=30 GLOB2_BENCH_FRAME_TIMES=1 GLOB2_BENCH_ZOOM="$zoom" \
 GLOB2_BENCH_PAN_X=145 GLOB2_BENCH_PAN_Y=190 GLOB2_BENCH_ADAPTIVE_ZOOM=1 \
 "$binary" --renderer legacy-gl --window-size 1280x800 --no-fullscreen --mute > "$out/trace.log" 2>&1
python3 - "$out/trace.log" <<'EOF'
import re, sys, statistics as st
t = open(sys.argv[1]).read()
f = [float(m) for m in re.findall(r'^FRAME mode=.*? frame=\d+ elapsed_ms=([\d.]+)', t, re.M)]
f.sort()
q = lambda p: f[min(len(f) - 1, int(p * len(f)))]
print(f"frames={len(f)} median={st.median(f):.2f} p95={q(.95):.2f} p99={q(.99):.2f} max={f[-1]:.2f} over33={sum(x > 33.3 for x in f)} over16={sum(x > 16.7 for x in f)}")
EOF
