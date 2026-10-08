# Final skin verification commands

Run from the repository root unless noted. Node/Python commands used the bundled primary runtime; Blender was the repository-pinned 3.6.23 arm64 build. See final-verification.json for source/base revisions, environment, counts and limits.

```sh
blender -b --python-exit-code 1 --python tools/skins/export_units.py -- --output artifacts/junction-rebuild/fair-export
python3 tools/skins/install_units.py artifacts/junction-rebuild/fair-export
blender -b --python-exit-code 1 --python tools/skins/export_views.py
# Preserve unchanged explorer/swarm metadata (floating-point-only regeneration noise).
git restore data/skins/colony-v1/explorer-fly.view.json data/skins/colony-v1/swarm*.view.json platform/apps/web/public/skins/models/explorer-fly.view.json platform/apps/web/public/skins/models/swarm*.view.json src/online/SkinViewTransforms.h
blender -b --python-exit-code 1 --python tools/skins/test_surface_geometry.py -- --root artifacts/junction-rebuild/fair-export --report artifacts/junction-rebuild/final-baked-audit.json
python3 tools/skins/fit_unit_shapes.py --model worker --output artifacts/junction-rebuild/round-shapes/worker
python3 tools/skins/fit_unit_shapes.py --model warrior --output artifacts/junction-rebuild/final-shapes/warrior
python3 tools/skins/install_rigs.py artifacts/junction-rebuild/round-shapes/worker artifacts/junction-rebuild/final-shapes/warrior
blender -b --python-exit-code 1 --python tools/skins/author_explorer_rig.py -- --output artifacts/junction-rebuild/round-explorer
python3 tools/skins/install_rigs.py artifacts/junction-rebuild/round-explorer
blender -b --python-exit-code 1 --python tools/skins/test_surface_geometry.py -- --root data/skins/colony-v1 --fitted --report artifacts/junction-rebuild/final-fitted-audit.json
python3 tools/skins/test_fit_shapes.py
python3 tools/skins/test_connected_motion.py
python3 test/build_system/test_skin_assets.py
python3 test/build_system/test_skin_surface_contract.py
python3 test/build_system/test_skin_materials.py
# In platform/:
npx vitest run apps/web/test/skin-detail-uv.test.ts apps/web/test/skin-projection.test.ts apps/web/test/skin-rig.test.ts apps/web/test/skin-shapes.test.ts
npx vitest run apps/web/test/skin-document.test.tsx apps/web/test/skins-workspace.test.tsx
npx tsc -p apps/web/tsconfig.json --noEmit
npx eslint apps/web/src/skins/geometry.ts apps/web/src/skins/MeshPreview.tsx apps/web/test/skin-detail-uv.test.ts
npx prettier --check apps/web/src/skins/geometry.ts apps/web/src/skins/MeshPreview.tsx apps/web/test/skin-detail-uv.test.ts
# Back at the repository root, using the compiled production-renderer harness:
GLOB2_USER_DATA_DIR="$PWD/artifacts/junction-rebuild/native-test-profile" GLOB2_TEST_ARTIFACTS="$PWD/artifacts/junction-rebuild/native-tests-artifacts" artifacts/junction-rebuild/native-skin-tests --test-suite-exclude=SkinReadback
# Existing host framebuffer comparison failure, kept for both renderers:
GLOB2_USER_DATA_DIR="$PWD/artifacts/junction-rebuild/native-test-profile" artifacts/junction-rebuild/native-skin-tests --test-suite=SkinReadback
GLOB2_USER_DATA_DIR="$PWD/artifacts/junction-rebuild/native-test-profile" artifacts/junction-rebuild/native-baseline-tests --test-suite=SkinReadback
# Each worker/warrior clip, using its default GSB and optional GUV:
artifacts/junction-rebuild/skin-preview-native data/skins/colony-v1/worker-walk.gsb artifacts/junction-rebuild/native-worker-walk --rig-review
# Repeat for worker-swim, worker-harvest, warrior-walk, warrior-swim, warrior-fight.
```

Native compilation reused SCons GAG objects and its exact dependencies after an initial `GLOB2_SDL3_PREFIX=... scons -j4 release=1 server=0 skin-preview` compiled the renderer but entered long first-time artwork generation. The complete packaging target was stopped; it is not claimed as a completed build. Changed production objects were recompiled with compile-changed-native.json; native-link-command.txt and native-tests-link.txt record the complete link commands; native-test-compile-commands.json records the final harness compilation. The harness compiled the repository's SkinMesh/SkinModel/SkinShapeModel/SkinModelRender tests. The baseline comparison replaces GraphicContextSkinMesh.cpp with its pre-GUV version from 14ec72376; the same 1,813 readback comparisons fail on this host. No failures were waived in the other 19 native cases.

The WebGL2 page uses the native shared deformation and material GLSL with a distinct paint/detail UV fixture. CPU and rig pixels match exactly; reverting detail to paint UV changes 10,141 channels over 3,444 covered pixels. This checks shader compilation and stream parity, not a complete compiled WebAssembly game build.

The final worker exports are byte-identical to the rounded worker reviewed earlier; their existing complete pose audit and GSB refit therefore remain applicable. The final warrior export additionally fairs its torso. Six native checker sheets contain all 256 frames for each default compressed clip at the production 128-pixel tile resolution. Source GSK/GSB/GUV bytes and their camera/provenance records are attached to the main PR; transient evidence stays on this branch.
