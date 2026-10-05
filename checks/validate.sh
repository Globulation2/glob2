#!/usr/bin/env bash
set -euo pipefail
export LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib
python3 test/run_tests.py --binary engine --filter 'TerrainMaterials/*' --filter 'TerrainPresentation/*' --filter 'TerrainProperties/*' --filter 'SoftwareRenderer/*' --filter 'TerrainValidation/*' --filter 'HighResolutionIntegration/*' --filter 'PortableRenderer/*' --filter 'MapRenderGeometry/*' --filter 'Torus*/*' --filter 'MapPreview/*' --filter 'TeamStatsSave/*' --filter 'MatchSetup/*' --filter 'TerrainEcology/*' --filter 'WindowResize/*' --filter 'MapRenderResize/*' --artifacts artifacts/terrain/review-final --junit artifacts/terrain/review-final.xml --timeout 300 -j4 --display-jobs 1 > artifacts/terrain/review-final.log 2>&1
python3 test/run_tests.py --binary unit --filter 'Sprite*/*' --filter 'ImageAssets/*' --filter 'AssetLoader/*' --filter 'Replay*/*' --filter 'PlatformProtocol/*' --filter 'FertilityField/*' --filter 'MusicBuffer/*' --filter 'MusicProducer/*' --filter 'OpaqueRectangleBatch/*' --artifacts artifacts/terrain/review-unit --junit artifacts/terrain/review-unit.xml --timeout 300 -j4 --display-jobs 1 > artifacts/terrain/review-unit.log 2>&1
python3 test/test_map_image.py build/linux/client/release/src/glob2 > artifacts/terrain/review-map-image.log 2>&1
python3 artifacts/terrain/benchmark-review-final.py > artifacts/terrain/review-final-benchmarks.log 2>&1
