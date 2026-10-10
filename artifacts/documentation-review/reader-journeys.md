# Reader journey review

Read-only tracked-file review starting at `docs/README.md`. This ignored file is review evidence, not a maintained guide. No game builds, package installs, public-service changes, or platform runtime tests were performed.

## Build a native client

Actual route: `docs/README.md` → `docs/development/README.md` → `docs/development/building.md`.

The development index names Build and run first. The build guide offers concrete macOS/Homebrew, Ubuntu 24.04 and Windows/MINGW64 dependency recipes, the pinned SDL3 bootstrap, environment/prefix selection, build command, exact executable path, and corresponding test runner directory. The routine workflow explicitly reminds readers to preserve platform-specific flags. Browser and mobile alternatives are directly linked.

Source basis: `tools/dev_build.py`, `scons/build_layout.py`, `scons/sdl3_dependencies.py`, native workflow dependency steps and `test/run_tests.py`. Package-manager installation and fresh builds were not executed on the three platforms, so this is a source-based journey assessment, not fresh-machine qualification. No navigation blocker found.

## Run or add native tests

Actual route: `docs/README.md` → `docs/development/README.md` → `test/README.md` → `docs/development/testing/README.md` → selected subsystem runbook.

The entry guide explains the two binaries, isolation, tags, focused selection, test registry, fixtures and adding cases. Its build/run example uses a custom directory and exports `GLOB2_BUILD_DIR`, avoiding the release-directory default mismatch for fast builds. The verification hub lists simulation, persistence, AI, maps, rendering, audio, scripting, telemetry and integration/tooling scenarios separately.

Source basis: `test/tests.py`, `test/SConscript`, `test/run_tests.py`, `tools/build_paths.py`. Focused tests are discoverable without searching unrelated runbooks. Test commands and harnesses were inspected, not executed against fresh binaries. No navigation blocker found.

## Change simulation safely

Actual route: `docs/README.md` → `docs/architecture/README.md` → `docs/architecture/overview.md` → `docs/development/simulation-verification.md` → `docs/development/headless-replays.md` and selected `docs/development/testing/*.md` guides → `docs/development/verification.md#local-and-vm-pr-verification`.

An equally direct route is documentation index → development index → Simulation verification. The guide separates behavior-preserving work from intentional changes; requires per-tick comparisons, serial/threaded and affected-platform coverage; distinguishes save, replay and network gates; and directs the contributor to sim-revision and golden-record handling. The headless guide supplies verify-match syntax and identifies fixture regeneration through `--update-fixtures`. The evidence template states exact revisions, environment, flags, commands, results, retained inputs and platform omissions.

Source basis: `src/game/SimRevision.h`, `src/replay/ReplayReader.h`, `src/app/Version.h`, `src/online/SimVersion.cpp`, `src/net/turn/TurnEngineHarness.cpp` and `test/check_sim_revision.py`. No execution equivalence or cross-platform coverage was established by this documentation review. No navigation blocker found.

## Create and run scripts

Actual route: `docs/README.md` → `docs/scripting/README.md` → `docs/scripting/javascript.md#write-a-script` → `#load-and-check-sources` or `#installing-custom-ai-controllers-profiles-1-and-2` → `docs/scripting/javascript-api.md` → `docs/development/testing/scripting.md`.

The authoring guide includes a complete small callback, AI/scenario capability distinctions, syntax checking, editor installation and standalone CLI attachment. It distinguishes compile checks from actual game execution, supplies local AI imports/development-file linking, and describes persistent globals, scheduling, failures, determinism and save continuation. The API is a separate reference; verification has its own focused runbook.

Source basis: `src/scripting/javascript/ScriptCommand.cpp`, `examples/javascript/glob2.d.ts`, `examples/javascript/glob2-v2.d.ts`, `src/game/GameHeader.h/.cpp` and scripting test registry. The external starter project and live AI library were not fetched or exercised during this review.

A leftover sentence stating Profile 1 was unpublished was reported and removed by the lead during review. One minor factual wording issue was reported: the attach-map-script introduction called it replacement of a USL script, while `ScriptCommand.cpp` selects JavaScript mode and installs JavaScript source. The command is correct; this is a terminology correction, not a blocked authoring journey.

## Topic-index clarity

All thirteen required category indexes are direct links from `docs/README.md`, in the configured order.

| Index | Obvious reader task/routes |
| --- | --- |
| `docs/development/README.md` | Build/run, native tests, evidence, everyday implementation and diagnostics. |
| `docs/architecture/README.md` | Read ownership overview, then select simulation, AI, growth, rendering or persistence. |
| `docs/features/README.md` | Configure/play features; separate table for authoring each content family. |
| `docs/ai/README.md` | Identify controller families, develop an AI, then evaluate telemetry/ratings/tournaments. |
| `docs/map-generators/README.md` | Browse catalog, use CLI, design/add generators, inspect references, evaluate and repeat maps. |
| `docs/assets/README.md` | Choose a production workflow, then reference/provenance or portable community content. |
| `docs/scripting/README.md` | Author/install/debug, look up API, run verification. |
| `docs/multiplayer/README.md` | Understand stack, client behavior, transport, community content and platform development/operations. |
| `docs/browser/README.md` | Build/play/test, architecture/contracts, then accepted decisions. |
| `docs/mobile/README.md` | Android/iOS builds, tools, touch interfaces, verification, releases and published policies. |
| `docs/hosting/README.md` | Local setup, service/network/security configuration, operations/recovery, optional services and deployment examples. |
| `docs/releases/README.md` | Shared qualification/mirror policy, desktop/browser publication, then store/platform procedures. |
| `docs/tools/README.md` | Evaluation, asset creation and validation; links to domain guides and tools inventory. |

The documentation index also provides contributor/content-creator/operator/player reader paths. Player manuals correctly route to the public site instead of duplicating them in-repository. Legal policies and generated provenance preserve their required public/authoritative locations.

## Validation and limits

`PYTHONPATH=artifacts/docs-runtime python3 tools/check_docs.py` reported 304 documents, zero errors and 67 external links during this review. This establishes local destination, anchor and reachability validity under the checker, not factual accuracy or availability of external sites. External links, fresh dependency installation, actual gameplay, hosted CI execution and cross-platform simulation outcomes remain outside this review.

No unresolved navigation blocker found. Source-specific limits are recorded above; the small attach-map-script terminology correction was sent to the lead.
