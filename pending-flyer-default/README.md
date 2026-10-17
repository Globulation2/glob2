# PR 1197: default flyer-style steering for Pending building routes

Tested revision: 403404a2badd878783b58f958d9eee6f2934dc37. Base: 486a2230a4ddb257ba9dee34d3aace05f2260d6f (origin/master, fetched before final verification). Parent: d1c0d4d50109b6f7d30f843368d30406f515b84f.

Assigned ground units retain their job and use the flyer's wrapped eight-direction heading and turning preference while a building route is Pending. Ground collision, terrain and forbidden areas remain enforced. Units wait if all directions are blocked and resume gradient routing after publication. Food/healing destination selection remains unchanged. This changes simulation behavior; SIM_REVISION is 73, save/replay version 174, save floor 58. Save layout is unchanged from 172.

## Environment and build

macOS 26.6.2, arm64 Apple M3; Apple clang 21.0.0. Managed dependency prefix build-dd8d40b0b2e044b88374d32e plus installed Homebrew dependencies. Development build: `dev_fast=1`, `-O0 -g1`, compiler caching, all locally supported features. No runtime performance claim is made from these builds.

Build command: `PYTHONPATH=. python3 tools/dev_build.py -j4 engine-tests unit-tests build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2`.

## Verification

engine: 146 cases, 0 failures, 0 skipped; compatibility: 41 cases, 0 failures, 0 skipped; turn: 18 cases, 0 failures, 0 skipped; movement: 103 cases, 0 failures, 0 skipped; preflight: 8 cases, 0 failures, 0 skipped; golden: 1 cases, 0 failures, 0 skipped. Commands are in commands.sh; match and old-save commands are also recorded verbatim in matches/comparison.json. Logs, JUnit, save fixtures, replay recordings and per-tick world/entity checksums are included in the archives. Map and binary hashes are recorded. The new regressions cover flyer turn preference around occupied ground, fully blocked waiting, forbidden areas, pure-swimmer/immobile limits, wrapped headings, flag/clearing assignment preservation, unchanged movement RNG, publication handoff, and binary/text save continuation after actual pending movement.

## Coverage and limits

Focused native checks cover the changed unit movement paths and the existing background gradient/hiring behavior, save continuation, replay acceptance boundaries and simulation/network version gates. The committed match trace is regenerated and independently checked by the turn-engine suite. Fresh matches compare every recorded world/entity checksum across one and four compute participants; older format-172 saves also load and continue identically across those participant counts.

No Windows/Linux/browser/mobile per-tick comparison, GPU comparison, rendered/manual play review, full test matrix or new release timing benchmark was performed in this update. Prior measurement evidence was counterfactual and does not establish actual CPU, travel-time or outcome improvement. Terrain obstacles can temporarily steer a unit away from the true route; this limitation is accepted by the chosen default. The PR remains draft for incomplete platform and manual-play coverage.

## Reproducing the comparisons

Check out the tested commit and build using the command above. `commands.sh` records the test commands, including the earlier fixture regeneration steps. `compare_matches.py` is the standalone standard-library script used for the participant comparisons. Before running it from the checkout, unpack `match-artifacts.tar.gz`, copy its `inputs/format172-{pending,queued}.game` into `artifacts/lazy-restore/`, and ensure the comparison output directory `artifacts/flyer-default/matches` does not already contain CLI output. The bundled maps come from the tested checkout; their hashes are in match-comparison.json. The native harness save files in test-artifacts.tar.gz are Game-only saves used directly by the harness loaders, whereas the old input saves include the GUI footer needed by the CLI.

The focused preflight and golden regeneration preceded the fallback commit. Broader validation exposed eight service/carrier fixtures that assumed synchronous cold-route availability and a stale unit-ablation golden trace. The follow-up commit updates those fixtures to wait for publication (with normal game ticks in active-world conversion fixtures) and refreshes the trace. Initial failures and intermediate diagnostics are retained. The final committed build and main engine, compatibility, turn, movement and match checks use the tested revision above. The initial build log retains the corrected const-reference compile failure; build-verified shows the successful final build.
