# Swarm shape choice: verification evidence

Tested commit 749abc4d91decea223b4a27df98a7bd70bc25cce (branch `skins-swarm-variants`),
merged with master feaae0bd86e8de99b7aa9dffce63db01dcbc023b.

Environment: Ubuntu 26.04.1 LTS, Linux 7.0 x86_64, GCC 15.2.0, `scons release=1 server=0`,
pinned SDL3 prefix; Python 3.14.4; Node 22.23.3 (official build); PostgreSQL 16.15;
Blender 3.6.23 (official build, `-t 1`) for mesh generation. Game captures ran under Xvfb
with Mesa llvmpipe.

## Results (all passed)

| Check | Command | Result | Log |
| --- | --- | --- | --- |
| Native build | `scons -j28 release=1 server=0 tests skin-preview skin-game-preview` | built | `logs/native-build.log` |
| Native skin suites | `test/run_tests.py --filter '*Skin*' --no-display` | 5 suites passed (SkinAuthorization incl. new swarm mesh case, SkinDownloads, SkinMesh, SkinAtlasCache, BuildingFailureDisplay) | `logs/native-tests.log` |
| Shipped mesh contract | `tools/skins/test_export.py data/skins/colony-v1` | all meshes, provenance, catalog/protocol/generator agreement verified | `logs/asset-validator.log` |
| Asset tests | `test/build_system/test_skin_assets.py`, `test_web_assets.py` | OK (1 skipped) | `logs/test_*.log` |
| Platform | `tsc` (api + web), `eslint`/`prettier --check` on changed TS, `vitest run apps/api/test/skin apps/api/test/accountExport apps/api/test/deletion apps/api/test/matches apps/api/test/realtime apps/worker/test/reliability packages/protocol packages/db` | 20 files, 112 tests passed | `logs/platform-tests.log` |
| Web browser | `npm run e2e -w @glob2/web -- skins.spec.ts smoke.spec.ts -g "colony\|skin\|draft\|touch targets\|reopens\|reports"` (desktop + phone) | 13 passed | `logs/web-e2e.log` |
| In game | `skin-game-preview` on `test/fixtures/team-stats/version129-muka-1100.game.gz` with the installed `data/skins/colony-v1` meshes, spots paint, `GLOB2_SKIN_PREVIEW_SWARM=<id>` for each of the 7 meshes | all rendered | `in-game/`, `in-game-all-shapes.png` |

`swarm-paint-metrics.jsonl` is `tools/skins/swarm_metrics.py data/skins/colony-v1/swarm*.gsk`:
the six generated shapes have no visible UV seams and uniform paint detail
(2.35-2.77 texels per screen pixel at 100% zoom); the classic swarm has 1,822 px of
visible seams and 0.75 texels per pixel.

Earlier during development a full `a11y.spec.ts` + `smoke.spec.ts` run had one failure in
"a file that is not a map is refused on the form" (map upload, unrelated to skins);
it passed when rerun alone on both viewports.

## Not covered

- No simulation, save or replay change (view-only); no checksum comparison was needed.
- Not run on Windows, macOS, Android, iOS or the WebAssembly build; browser
  `colony-skins.spec.js` was not run.
- Native rendering was checked with llvmpipe only.
