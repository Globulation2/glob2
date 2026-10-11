#!/usr/bin/env bash
set -euo pipefail
if (( $# == 0 )); then set -- final-baseline final-ab; fi
for variant in "$@"; do
 mkdir -p "artifacts/memory-wins/$variant/test"
 ln -sfn ../glob2-unit-tests "artifacts/memory-wins/$variant/test/glob2-unit-tests"
 ln -sfn ../glob2-engine-tests "artifacts/memory-wins/$variant/test/glob2-engine-tests"
 ln -sfn "$PWD/build/linux/client/release/client-full-direct-1111/runtime-assets" "artifacts/memory-wins/$variant/runtime-assets"
 env -u DISPLAY -u WAYLAND_DISPLAY -u WAYLAND_SOCKET XDG_SESSION_TYPE=x11 LD_LIBRARY_PATH="$PWD/artifacts/memory-wins/lib/glob2" python3 test/run_tests.py --build-dir "artifacts/memory-wins/$variant" --binary unit --filter 'AssetLoader/*' --filter 'ImageAssets/*' --filter 'SpriteLoad/*' --filter 'SpriteSheets/*' --filter 'GpuTexturePreparation/*' --junit "artifacts/memory-wins/$variant-unit.xml" --artifacts "artifacts/memory-wins/$variant-unit-captures" > "artifacts/memory-wins/$variant-unit.log" 2>&1
 env -u DISPLAY -u WAYLAND_DISPLAY -u WAYLAND_SOCKET XDG_SESSION_TYPE=x11 LD_LIBRARY_PATH="$PWD/artifacts/memory-wins/lib/glob2" python3 test/run_tests.py --build-dir "artifacts/memory-wins/$variant" --binary engine --filter 'EngineSession/automatic GUI sessions poll*' --filter 'HighResolutionIntegration/*' --filter 'UnitHighResolutionCache/*' --junit "artifacts/memory-wins/$variant-engine.xml" --artifacts "artifacts/memory-wins/$variant-engine-captures" > "artifacts/memory-wins/$variant-engine.log" 2>&1
 echo "$variant native checks passed"
done
