#!/usr/bin/env bash
set -euo pipefail
root="$PWD/artifacts/skin-sprites"
binary="$PWD/build/linux/client/release/src/glob2"
export LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2
build/linux/client/release/test/glob2-unit-tests --test-suite=SkinAuthorization,SkinDownloads,SkinSprites,SkinMesh,SkinMaterialMap,SkinAtlasCache,SurfaceCoverage,ImageAssets,RenderFramePacer --test-case-exclude='*[display]*' > "$root/native-final-revision.log" 2>&1
GLOB2_SKIN_EXPORT_DIR="$root/current-export" SDL_VIDEODRIVER=x11 xvfb-run -a build/linux/client/release/test/glob2-unit-tests --test-suite=SkinReadback > "$root/readback-final-revision.log" 2>&1
SDL_VIDEODRIVER=x11 xvfb-run -a build/linux/client/release/test/glob2-engine-tests --test-suite=SoftwareRenderer,PortableRenderer > "$root/renderer-final-revision.log" 2>&1
"$binary" --verify-match test/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out "$root/verify-final-revision" > "$root/verify-final-revision.log" 2>&1
cmp "$root/verify-final-revision/checksums.txt" test/fixtures/multiplayer/FourSquares1.verify-trace.txt
xvfb-run -a "$binary" --render-skin --manifest "$root/green-input/manifest.json" --texture "$root/green-input/texture.png" --material "$root/green-input/material.png" --output-dir "$root/current-green-export" > "$root/green-final-export.log" 2>&1
for mode in cpu gpu; do
  flag=-G; frames=240; warmup=180
  if [ "$mode" = gpu ]; then flag=-g; frames=48; warmup=32; fi
  for zoom in 1 2; do
    name="$mode"; if [ "$zoom" = 2 ]; then name="${mode}2"; fi
    GLOB2_USER_DATA_DIR="$root/profile-$name" SKIN_PREVIEW_SAVE="$root/crowd-three.game.gz" SKIN_PREVIEW_ASSIGNMENT="$root/assignment.json" SKIN_PREVIEW_CACHE="$root/benchmark-cache-$name" SKIN_PREVIEW_ZOOM="$zoom" SKIN_PREVIEW_CAPTURE="artifacts/skin-sprites/$name-final.bmp" SKIN_PREVIEW_BENCHMARK="artifacts/skin-sprites/$name" SKIN_BENCH_TEAMS=3 SKIN_BENCH_FRAMES="$frames" SKIN_BENCH_WARMUP="$warmup" SKIN_BENCH_FRAME_PREFIX="artifacts/skin-sprites/$name-frame" xvfb-run -a build/linux/client/release/src/skin-game-preview "$flag" -m -s800x600 > "$root/$name-final-benchmark.log" 2>&1
  done
done
