# **Globulation 2 developer-experience revamp: game plan**

Status: plan, not started. Written 2026-11-06 from a nine-agent measured analysis of the repository at master `992e9ef8d7` / branch `fix/macos-openssl4-build` (`1384f0900f`). This document is meant to stand on its own: it records the problem, the evidence, the decisions, the target state, every workstream as PR-sized steps with dependencies, and how to verify the result. Re-read section 3 (findings) before starting any phase; the numbers there are the baseline to beat.

---

## **1\. Purpose**

Development on Glob2 has slowed to the point where compiling and running tests is roughly 97% of wall-clock development effort. The maintainer (Bradley, solo, working through many parallel AI-agent lanes, \~18 merged PRs/day) reports: small header edits rebuild most of the game; performance work forces release builds and side-by-side master/branch worktrees; more than three parallel lanes fight each other; CI is so slow that it is cancelled at merge; and the disk fills up repeatedly (it hit 100% full, 0.3 GiB free, during this analysis).

The plan's goal is a development loop where:

- an ordinary edit recompiles a handful of translation units (TUs), not hundreds;  
- a new worktree builds in well under a minute from shared cache, not \~7–28 minutes;  
- N lanes on one machine share the CPU at one-lane efficiency;  
- performance work uses a `perf=1` build and a prebuilt master baseline instead of two release worktrees;  
- a required CI check finishes in ≤15 minutes and master is kept green;  
- tests for a module can be built and run without linking the whole game;  
- disk use is bounded and visible, and merged worktrees do not linger.

Scope decided by the maintainer: "reasonably deep, beyond quick wins, less than a full rewrite." Specifically: the 230-object link tangle IS in scope as the late phase; a staged SCons→CMake+Ninja migration IS planned (late); the per-PR evidence-retention policy is dropped (evidence lives in the PR description or nowhere); merges will wait for a fast required check and a "master red \>24 h → next merge repairs or reverts" rule is adopted; evidence refs in git are expired and the repo repacked, accepting broken links.

---

## **2\. Background: how the analysis was done**

Nine parallel analysts worked read-only on the checkout on 2026-11-06, 08:00–10:00, under heavy external load (load average 13–68 on 8 cores, a concurrent disk cleanup, and the disk at 100% full for part of the window). Consequently:

- **Counts, ratios, CPU-seconds and static graph numbers are reliable.** Wall-clock times are inflated and marked "contended". A clean full-build wall time, link time and header-touch rebuild time were NOT measured (the timed build was stopped at 460/760 TUs on the maintainer's instruction). Re-measure those on a quiet bench machine (therig, pharaoh-dev-2/3) before and after Phase 1 if absolute numbers are wanted.  
- Labels used throughout: **\[M\]** measured on this machine; **\[S\]** static include graph (validated against `clang -M` for the top headers: Map.h 403 in both, json.hpp 474 vs 472); **\[E\]** estimate (what-if simulation on the graph, regex heuristic, or extrapolation).

Methods (all scripts and CSVs were left under `/private/tmp/claude-501/-Users-bradley-glob2/91c4dabb-64c0-4553-86d4-44903fdb686d/scratchpad/analysis/`; that directory is session-scoped and may be gone — the numbers that matter are reproduced below, and the methods are simple enough to redo):

- **Include graph**: Python resolver of quoted `#include`s against `INCLUDE_DIRECTORIES` from `scons/sources.py`/`SConstruct`; transitive TU fan-in per header; cross-checked with `clang++ -M`; `-ftime-trace` on 458 TUs; `-E | wc -l` for preprocessed weight.  
- **History replay**: for each of 1,205 first-parent master commits since 2026-09-01 the include graph was rebuilt from git blobs at that commit and the "blast radius" (TUs that would recompile) computed; counterfactuals removed specific include edges on the current graph. Tools: networkx, lizard, pandas.  
- **Link graph**: `nm` symbol graph over 995 objects of an existing dev build → Tarjan SCC; per-test reachability.  
- **Build system**: four scratch worktrees of master (with the OpenSSL 4 fix cherry-picked, since master does not build on this Mac), scratch `CCACHE_DIR`, `/usr/bin/time -l`, `scons --debug=explain`, 1/2/4-lane contention runs.  
- **Policy/CI**: all 1,222 PRs merged 09-01..11-06 (`gh`), 5,787 `build.yml` runs, 84 runs' job timings, regex classification of PR bodies (counts are lower bounds).  
- **Scripting**: QuickJS-NG vs C++ `-O2` microbenchmarks on generator-style and AI-style kernels (CPU time, identical results).  
- **Disk**: `du` over every worktree, build dir, store, cache and git object store; `git` blob census.

---

## **3\. Findings (the baseline)**

### **3.1 Rebuild blast radius is large and growing fast \[M/S\]**

- 1,207 TUs (808 production, 399 test); the client build is 745 TUs. The median TU pulls in 1,317 files, 125 of them project headers; **the median TU reached 93 repo headers in November vs 19.5 on Sept 1\.**  
- 105 project headers each reach ≥50% of TUs; 20 reach ≥70%; 132 reach \>40%.  
- Code commits (736 since 09-01): median rebuilds 8.4% of TUs, p75 52%, p90 66%. **28% of commits rebuild \>50% of TUs and those cause 75% of all 165k TU recompiles (≈178 full builds).** Cost-weighted mean blast per commit: 9.6% (Sept) → 16% (Oct) → 27% (Nov). Last 30 days: 26% of PRs rebuilt \>80% of compile cost.  
- 30% of the last 1,000 C++ commits edited a header with ≥300 TU fan-in.  
- **Compile time is 94% frontend (parse \+ template instantiation), 5% backend \[M\].** Optimisation level is not the bottleneck; header weight is. `std::vector` instantiations ≈1,865 trace-seconds; `nlohmann::json` ≈1,800 (used by 15% of its includers).

Top culprit headers (fan-in \= client TUs of 745 \[M\]; recompiles \= TU recompiles caused since 09-01 \[S\]):

| Header | Fan-in | Recompiles caused | Why it is everywhere |
| :---- | :---- | :---- | :---- |
| `BuildConfig.h` | 608 | – | via `GAGSys.h`; holds absolute paths (see 3.3) |
| `GAGSys.h` | 603 | – | pulls `<SDL3/SDL.h>`; included by `UnitConsts.h`, `UnitType.h` |
| `SDLGraphicContext.h` (via one-line re-export `GraphicContext.h`) | 549 | 34.0k | `GameEvent.h`/`BaseTeam.h` include it only for the 4-byte `GAGCore::Color`; drags `<iostream>` \+ gag render stack into 783 TUs incl. sim code |
| `FileFormatVersions.h` | 504 | 26.6k | via `Version.h`, `SaveLoadProfile.h` |
| `Version.h` | 499 | 33.3k | **every change is a `VERSION_MINOR` bump**; 700 of 713 reaching TUs come through one edge, `TeamStat.h:434` default arg `= VERSION_MINOR`, added 2026-10-13 in \#1152 |
| `Team.h` | 491 | – | `BasePlayer.h → GameHeader.h → WorldSnapshot.h`; `Map.h`; `Building.h`; 24 of 127 includers use only constants |
| `TerrainType.h` \+ `TerrainTypeTable.h` | 492 | 37k | built-in terrain enum/table; grows with every new terrain; "add a terrain" co-changes Version.h, FileFormatVersions.h, ReplayReader.h in 17–20 PRs each rebuilding ≥60% |
| `json.hpp` (85k lines) | 472 | 0 | `WorldSnapshot.h → MapAssetBundle.h` though it holds only a `shared_ptr` |
| `AI.h` | 446 | – | `BasePlayer.h` for the implementation-id enum |
| `ComputeExecutor.h` (1,500 inline lines) | 414 | – | by-value member of `Map` (`Map.h:118`) |
| `Building.h` | 406 | 16.5k | included by `Map.h`, which uses Building only through pointers (34 of 38 mentions); includes `map/MapInternal.h` |
| `Map.h` | 403 | 31.4k | genuine (73% of includers use it); 111 inline bodies are tiny hot accessors; cost is its 23 includes |
| `Scene.h` / `MapRenderState.h` | 371 | – | **`Game.h:33` includes `render/MapRenderState.h` and `Game::ViewState` embeds it by value → sim core drags the renderer** |
| `Game.h` (108k preprocessed lines) | 365 | 27.4k | genuine; most expensive header ≈26% of traced compile time; \~35 `static draw*` members force render TUs to include it |
| `SimRevision.h` | 111 | – | `SimVersion.h:83` default arg `= SIM_REVISION` defeats its own "kept apart from Version.h" comment; bumped 73 times |

Hot accessors that must stay inline (release has **no LTO**: `SConstruct:46`, `scons/size_optimization.py:18`): `coordToIndex` (85 hot call sites), `terrainPropertiesAt` (56), `getBuilding`, `getGroundUnit`, `resourceBlocks*`, `Building::availableMaterial`/`materialNeed`. Measured: moving a per-cell accessor out of line without LTO was 4–40× slower; ThinLTO/full LTO inlined all sites back; one extra pointer load per *query* (not per cell) was within noise \[M\].

The stable-core pattern already exists: `src/field/` reaches none of Map/Game/Team, and `MapStateView.h` (free inline functions over plain data) changed 3 times while `Map.h` changed 71\.

Counterfactuals \[E, on current graph\]: cut `TeamStat→Version.h` (2 edges) −4.3% of recompiles; all version/format/sim-revision constants reachable only from .cpp (16 edges) −8.1%; `GraphicContext.h` stops re-exporting SDL \+ `SceneEntities.h` forward-declares (2 edges) −10.2%; **those three together (33 edges) −23.9% recompiles / −21% cost**, and the share of commits rebuilding \>50% drops 19.7% → 12.9%; all seven cheap cuts −28.5%. "Map.h and Game.h never change" would have saved at most 11.3%, because 119 of the 189 heaviest commits touch two or more wide headers. Upper bound if map/game/app/gag headers never changed: −46.5%.

Stable-dependencies check: no layering exists — every domain component has instability ≈0.5 with Ca≈Ce≈20; libs/gag (I 0.05) and common are the only structural bottoms and both churn (gag: 136 header-changing commits). Production header edges form one strongly connected cluster of 19 top-level directories; a greedy cut of 119 header include edges breaks every depth-1 cycle. `map↔game` is held by `Map.h → game/Sector.h, game/ExperimentalFeatures.h`, `Team.h → GameEvent.h, WinningConditions.h`, `Game.h → Map.h`.

### **3.2 The link tangle \[M\]**

- 19 executables link the full client: the game, `glob2-engine-tests` (867 .o on the link line, 183–225 MB dev), 9 `PROGRAMS` in `test/tests.py`, 8 harnesses in `src/SConscript`. Client objects reach the linker as loose `.o` (no dead-strip). Only `libgag.a` and `libusl.a` are archives.  
- **One SCC of 230 objects (97 MB)**: Map, Game, Unit, GameGUI, GlobalContainer, all `map/pathfind`, 18 render objects, 71 AI objects (every AI). Median engine test needs 63% of production objects; 174 of 203 test objects reach GameGUI and GlobalContainer; `EngineFixtures.o` alone pulls 499 objects (HeadlessGame constructs GameGUI).  
- Welding edges: `Game_sync → ReplayWriter → GameGUI::save`; `GlobalContainer → ReplayReader → GameGUI::GameGUI`; `GameGUI → Game → Map → Game::drainHiring`; `GameAI → AIPipeline → AI.o` (factory switch constructs every AI); `GameDiagnostics → MapRender::toPng → GlobalContainer::loadOffscreenGraphics`; `GameAI → AIJavaScript → ScriptRuntime` (QuickJS); `Construction → TeamStep → Game_sync`. `globalContainer` is used in 141 non-test .cpp (gfx 140, buildingsTypes 101, settings 50).  
- Consequence: link-graph test selection cannot work — a change anywhere in the SCC reaches 88% of tests / 95% of test time. Link time itself is NOT a problem (ld-prime ≈1 s for the 185 MB binary) \[M\].

### **3.3 Build system and parallel lanes \[M\]**

- `tools/dev_build.py` → `scons dev_fast=1 linker=auto -j min(ncpu,8)`, ccache 12G, `-O0 -g1 -fPIC`, \~70 `-I` dirs, `CCACHE_BASEDIR=<root>`, `-fdebug-prefix-map`. Each worktree has its own `build/darwin/client/debug/dev-dev_fast-true-linker-auto/`.

| Case (glob2 target, 819 TUs) | Wall (contended) | CPU u+s |
| :---- | :---- | :---- |
| Cold build, empty cache | \~1,680 s | \~4,215 s (\~4.2 CPU-s/TU) |
| Second worktree, same commit, warm cache | 427 s | 1,269 s |
| Same worktree, `.o` deleted (direct hits) | 42 s | 68 s (compiles sum 41 s) |
| Fresh worktree, libgag only (76 TUs all hits) | 270 s | 905 s — compile 7 s; **\~260 s is configure \+ runtime-asset export** |
| No-op build | 27.7 s | 24.5 s, 790 MB RSS (7 s SConscript read, 18 s scan/signatures; `.sconsign` 43 MB) |
| Second build after a cold build | 41–44 s | **re-runs 749–792 of 819 compiles** ("X.cpp is no longer a dependency / is a new dependency"); only cheap because ccache absorbs it |
| Link glob2 alone | 0.75–1.3 s | negligible |

- **Cross-worktree cache hits are \~20× costlier than they should be (834 vs 41 CPU-s)**: the second worktree got 98% hits but only 1.9% *direct* hits, because `-DGLOB2_RENDER_SHADER_DIR="<absolute worktree path>"` is on every compile line (`scons/renderer_shaders.py:84`; only `libs/gag/src/GraphicContextRenderer.cpp` and `ProgrammableGLDeviceTest.cpp` read it; added in \#1115 a day after the ccache base\_dir work in \#1034) and `BuildConfig.h` holds absolute `PACKAGE_SOURCE_DIR`/`PACKAGE_RUNTIME_ASSET_DIR` (`SConstruct:98-99`; read only by `libs/gag/src/FileManager.cpp`) yet reaches \~47% of TUs via `GAGSys.h`/`ClientFeatures.h`.  
- **Lane contention**: libgag rebuild per lane, ccache off, `-j8` each: 1 lane 37 s wall / 97 CPU-s; 2 lanes \~135 s / \~127; 4 lanes \~222 s / 137\. Aggregate throughput fell from 1.62 to 1.07 lane-builds/min; **CPU per identical job rose 40%** (pure contention). No jobserver/semaphore anywhere: every lane, dependency build, the asset-export pool (`jobs=num_jobs`) and `run_tests.py` (`-j cpu_count`) all use ncpu. Observed load 30–68, 3.5 GB swap, 98k pageouts, 5.4M involuntary context switches in one build.  
- **ccache**: 11.2 GiB vs configured 5.0 GiB (223%), **5,927 cleanups, 29% hit rate**; `scons/ccache.py` defaults 4G, `dev_build.py` sets 12G, bare ccache 5G; no `ccache.conf`. `CCACHE_BASEDIR` is set only for dev\_fast, so release/opt builds never share across worktrees. A full dev build is only \~0.1 GB compressed — size is not the issue, inconsistency is.  
- Runtime-asset export: 620 MB per worktree, 62,036 copied files (0 hard links), \~240 s / \~800 CPU-s per fresh worktree.  
- `Glob2.cpp` uses `__DATE__`/`__TIME__` (uncacheable, 9 s). `TestBuildProvenance.h` is regenerated from `git status` \+ full bytes of untracked files at every SConscript read (2.4 s) → any commit, branch switch or stray file (e.g. `300.log`) recompiles `TestMain.cpp` and relinks both test binaries.  
- Master does not build on this Mac: `LanIdentity.cpp:102` fails against OpenSSL 4; fix is `4b38a8422c` on `fix/macos-openssl4-build`.

### **3.4 Feature gating \[S \+ M\]**

Existing switches (`opengl=0`, `wss=0`, `client_profile`, `feature_*`, `recording_codec`) compile nothing out selectively, and the first two add defines to `BuildConfig.h` (reaches 608 TUs → every TU misses ccache). Generators (69 TUs, 14.6% of client weight, 111 commits / 51 focused), non-default AIs Cortex/Castor/Numbi/Cabino (31 TUs, 5.3%, almost never focused edits) and the editor (39 TUs, 8.0%) are **47–53% of every core-header rebuild** (Game.h 365 TUs \= 92 gen \+ 60 AI \+ 34 editor \+ 171 rest). Recording, GPU gradients, alt renderers, MCP, campaign/tutorial, JS are each ≤1.3% and some reach \~555 TUs via shared headers — not worth compiling out. Generator/AI TUs preprocess to 120–180k lines (same as core) because they include Game.h/Map.h. `WssTransport.cpp` (Boost) is the single heaviest TU (245 s contended, 297k lines). `SimVersion.cpp` does not encode which AIs/generators are linked, so a profile build must mark itself and refuse agent/verifier/rated roles. `defaultMethod()` prefers `lava-shield`; the golden match is Nicowar vs Warrush (needs Econo/shared\_runtime).

### **3.5 Tests \[M\]**

- 1,969 engine cases, 3,658 s serial (last green master, GCC 13); shards 1728/987/441/497 s because `test/ci-timings/*.json` are empty. 48 cases ≥10 s \= 69% of time; top 10 \= 44%; `[display]` 306 cases \= 65%; `[slow]` 20 cases \= 49%; **headless non-slow 1,652 cases \= 887 s (24%)**, median 0.10 s. `UIPresentation` 1,109 s in 6 cases (already `[slow]`; the real issue is that CI never passes `--quick`); `MapGeneratorDefaults` 309 s; golden `TurnEngineHarness` incl. verify is cheap (29 s). Only `HighResolutionIntegration … OpenGL` (64 s) is untagged.  
- `glob2-unit-tests` already links 167 test TUs \+ 116 production objects \+ stubs (46–66 MB) — the seed of per-module testing.  
- Coverage job takes 7,517 s; macOS compile 3,158 s.

### **3.6 Policy, PRs and CI \[M\]**

- Rules forcing release builds: `AGENTS.md` "Development and release builds" (release=1 for "representative performance measurements… never compare its runtime performance to release"); `build-options.md:41`; `rendering-benchmarks.md:3,182`, `software-rendering-benchmarks.md:3,6`, `memory-benchmarks.md:7-8`, `gradient-benchmarks.md:222` ("identical release builds"), `resource-benchmarks.md:153-163` (baseline rebuilt with test sources), `simulation-verification.md:11` ("optimized binaries… serial and threaded… affected platforms"), the cross-platform checksum rule for anything affecting "simulation results or portability".  
- `release=1` conflates `-O3 -s`, a no-op `-fwhole-program` (no `-flto`), `client_profile=free`, video sprite packing and lossy assets (`SConstruct:42-49, 359-364, 515-521`; `scons/runtime_assets.py:13-24`). Desktop has only `-O0` and `-O3`; web/android/ios ship `-O2` (`web_build.py:100`, `mobile_build.py:85`). Sim numeric sources already use `-fno-fast-math -ffp-contract=off` (`src/SConscript:53`), so \-O level should not move checksums. No in-repo \-O2 vs \-O3 comparison exists.  
- 1,222 merged PRs (66% from `codex/*`): first-commit→merge median 0.3 h (non-perf) vs **1.9 h perf, 3.3 h perf with release builds**; perf PRs 135 (11%), 7× median size, 24% mention release/O3, 16% a master baseline binary, 22% worktrees. 66 SIM\_REVISION bumps (≈20 → 128; 73 commits since 10-04), golden trace changed 50×, **26 refresh-only PRs**; 295 fix-style PRs (24%): 51 follow-ups, 46 master repairs, 65 platform fixes.  
- CI at merge: 16% last run green (Oct–Nov 5%), 9% red, 9% running, **65% cancelled at merge (deliberate — CI too slow; `cancel-superseded.yml`)**. Master pushes: Sept 176 ok / 31 fail / 63 cancelled; Oct 28 / 210 / 523; Nov 0 / 45 / 100 — i.e. since mid-October almost every *completed* master run is red (last green full runs 10-10, 10-16; nightly red 10-31..11-05). Full matrix: median 187 min wall (earlier green runs \~30 min), 1,065–1,610 runner-min, \~73 jobs. Job inflation: Windows 13.5 → 74–101 min, Clang coverage 81 → 143–154, Android 18 → 45, Linux build 13 → 41–45 (ccache misses suspected, consistent with 3.3). Browser test shards failed 154/308 on push; Windows 20/20 sampled PR runs. `ci-macos.yml` median 59 min, 27 fail / 16 ok. No branch protection or ruleset exists on master today.  
- The ML test predictor (`ci-model.yml`, 12% check-count budget) could not help: it selects jobs, and each selected job still builds the game (builds dominate); test time sits in 48 cases; the SCC makes every change reach 88% of tests; and selection only helps when master is green.

### **3.7 Scripting \[M\]**

QuickJS-NG (vendored, deterministic, already on the sim path via `AIJavaScript` and JS generators with a 773-symbol native toolkit binding) measured **70–385× slower than C++ \-O2** on 4-octave noise (6.7 vs 2,562 ms), 3×3 smoothing (7.1 vs 1,156), BFS flood (2.5 vs 180), AI scoring (84 vs 7,986), with identical results. Only **32 of 1,272 commits (2.5%)** since 09-01 confined their `src/` changes to AI or generator code; 2 of 216 AI/generator commits changed only numeric literals. Maxima already has hot-reloadable tuning layers (`content/data/maxima/base.strategy`, `GLOB2_MAXIMA_LAYERS/OVERRIDES`). **Decision: do not port Maxima or native generators to JS**; steer *new* generator concepts to the JS toolkit path; isolate native AIs/generators as libraries instead. Side finding to check: Apple clang 21 `-O2` compiled the benchmark's torus stencil loop into an ASan heap overflow with run-to-run-varying sums; `-O0`, `-O1`, `-O2 -fno-vectorize` were correct; no UB found. Unconfirmed; see workstream 3 step H1.

### **3.8 Disk \[M, as found 08:15\]**

\~190 GiB of glob2 data on a 460 GiB disk; the 32 GiB store budget governs only \~14 GiB of managed caches.

| Location | GiB |
| :---- | :---- |
| 16 retained worktrees (build 30.4, artifacts 10.0, source 24.8) | 65.3 |
| main `build/` (11 variant trees: darwin release 4.3 incl. dSYM 0.75; opt-tests 4.85; android 4.54; sprite-macos 4.09; asset-cache 2.78; sprite-android 2.42; three identical dev\_fast trees 1.6–1.8 each) \+ `build-software/` | 30.6 |
| dev store (ccache 11.2, toolchains 11.3, deps 1.4, other 1.3) | 25.2 |
| `.git/objects` (one 23 GiB pack; 565 blobs \>10 MiB \= 17.6 GiB, **15.8 GiB evidence archives; only 0.37 GiB reachable from master**; kept alive by `evidence/*`, 265 `codex/snapshots/*`, `turn-diffs`, `archive/*`, 1,122 remote refs) | 25.2 |
| main `artifacts/` (3.18 GiB `.ppm`, 0.23 `.bmp`, build trees inside artifacts 3.4, 65 standalone `gradient-*` campaigns 4.45 with 3,897 raw logs, `showcase/` 23 near-duplicates) | 23.6 |
| Homebrew android-commandlinetools (second Android SDK) | 11.4 |
| `/private/tmp` scratch worktrees of other sessions | \~2.5 |
| `~/.gradle`, `~/.npm`, `tools/browser-emsdk` | 5.2 |

Root causes: 10 of 16 worktrees are merged (6 squash-merged, invisible to `git branch --merged`) and still hold 20.6 GiB of builds (three worktrees on the same commit `64511a5602` hold 10.7 GiB); macOS release keeps full `-g` (`SConstruct` strips only non-Darwin) so release `.o` are \~4× dev (`WssTransport.o` 86 MB); Android stores four identical 727 MB `libmain.so` \+ 677 MB `.dbg`; `build/asset-cache` is per checkout and unbounded (133k entries); one agent worktree holds 670 BMP screenshots \= 6.17 GiB; only the tournament runner checks free space; `dev_environment.py status` takes 5 min and misses worktrees and git. New worktree after one dev build: 3.6–6.4 GiB.

---

## **4\. Decisions already taken**

1. Stay on C++ for AIs and generators; no JS port. New generator *concepts* go through the existing JS toolkit when they need no new hot primitive.  
2. No per-module dylibs, no faster linker (link ≈1 s). No compiling out recording/renderers/GPU gradients/online via `#ifdef`.  
3. Hot accessors stay inline in headers; release stays non-LTO static. Every architecture step is simulation-neutral: `SIM_REVISION` unchanged (128), committed `match verify` trace byte-identical, per-tick checksums identical on macOS arm64 \+ Linux x86-64.  
4. The 230-object tangle is broken as the late phase, ending in `libglob2_map.a`/`libglob2_sim.a` and per-module test binaries.  
5. SCons stays for now and gets tuned; a staged CMake+Ninja migration is planned as the final build-tooling phase.  
6. Build ladder: dev (`-O0`) / perf-screen (`perf=1`, `-O2`) / perf-confirm (`perf=1 opt=3`) / release (shipping only). Release measurements are never PR perf evidence.  
7. Merges wait for a required fast gate (≤15 min). Master red \>24 h → next merge must repair or revert. The ML predictor is retired, not repurposed.  
8. Evidence retention is dropped: evidence lives in the PR description or is not kept. `evidence/*`, `codex/snapshots/*`, `archive/*` refs are expired and the repo repacked; broken links accepted.  
9. Cross-platform checksum verification narrows to an explicit simulation path list; outside it, no cross-platform run is required.

---

## **5\. Target state**

Layering (bottom → top), enforced by a CI check with a ratcheting allowlist:

```
L0  third_party; libs/gag core types (GAGColor.h, no SDL umbrella); src/common
L1  resource + unit/building type catalogues (data), TeamCapacity.h, UnitConsts.h, FieldReadiness.h,
    BuildingRoute.h, MapTypes.h, TerrainCatalog.h (terrain table as data)
L2  grid core: MapState, MapStateView, src/field/, MapGrid (force-inlined hot accessors)
L3  Unit, Building (forward-declare Team/Game; cached pointers for hot cross-object reads)
L4  lean Map.h (routing, editor, generator, fog in separate headers; ComputeExecutor behind unique_ptr)
L5  Team, data-only Game, SimContext, GenerationWorld; engine/sim
L6  snapshot/scene records (never include Game.h); ai, scripting
L7  render (MapDrawer), hud, ui, net, online, replay
L8  app (Settings, GlobalContainer). No header layer holds version constants.
```

Build/dev loop: one `ccache.conf`; path-independent keys; shared asset export cloned per worktree; a machine-wide CPU token pool; a prebuilt master baseline per SHA; `compile_profile=core` for focused work; `dev_environment.py worktrees|disk|gc` commands; free-space guard. CI: `ci-fast.yml` required (contracts \+ Linux dev build \+ headless tier \+ sim-revision check \+ master-health), full matrix async on master \+ nightly, `ci:full` label on demand. Tests: `--tier headless|display|full`, populated timing profiles, coverage-based `--affected`, per-module binaries after the tangle is cut.

---

## **6\. Workstream 1 — build sharing, storage, build tooling**

Owner files: `tools/dev_build.py`, `scons/dev_build.py`, `scons/dev_compile.py`, `scons/dev_store.py`, `scons/ccache.py`, `scons/shared_dependencies.py`, `scons/runtime_assets.py`, `scons/renderer_shaders.py`, `scons/client_features.py`, `scons/sources.py`, `scons/build_layout.py`, `SConstruct`, `src/SConscript`, `tools/dev_environment.py`, `docs/development/build-options.md`, `docs/development/development-storage.md`.

| Step | Goal and change | Gain | Risk / verification | Effort |
| :---- | :---- | :---- | :---- | :---- |
| **W1-1 Path-independent ccache keys** | New `scons/build_paths.py` writes `BUILDDIR/include/glob2/BuildPaths.h` (same `Configuration`/`env.Command(Value)` pattern as `BuildConfig.h`) holding exactly `PACKAGE_SOURCE_DIR`, `PACKAGE_RUNTIME_ASSET_DIR`, `GLOB2_RENDER_SHADER_DIR` (web `"/"`, `"/shaders"`; mobile `"."`). Remove those three from the `BuildConfig.h` writers (`SConstruct:98-99`, `scons/web_build.py:79-88`, `scons/mobile_build.py:67-73`) and from `renderer_shaders.configure`'s `CPPDEFINES` append (`scons/renderer_shaders.py:84`). The only consumers add `#include <glob2/BuildPaths.h>` under the existing `HAVE_CONFIG_H` guard: `libs/gag/src/FileManager.cpp` (:26-30, :115-117, :164-165), `libs/gag/src/GraphicContextRenderer.cpp:31`, `libs/gag/src/ProgrammableGLDeviceTest.cpp:34`, `test/support/Glob2Test.cpp:89`. `PACKAGE_DATA_DIR` stays (not worktree-dependent). Also replace `__DATE__/__TIME__` in `src/app/Glob2.cpp:703` with `GLOB2_BUILD_REVISION` from a generated `BuildStamp.h` (git HEAD \+ dirty flag, `write_if_changed`) so the 9 s TU becomes cacheable and PCH sloppiness is no longer needed. | Fresh-worktree compile **834 → \~41 CPU-s \[M\]**, 427 s → \~60 s wall; every lane rebase stops costing other lanes \~800 CPU-s | Only 4 objects may change: diff `sha256` of all `.o` before/after; `strings glob2 | grep $PWD` unchanged; `CCACHE_DEBUG=1` on a second worktree shows ≥95% direct hits; run `RuntimePackCheck`, `ProgrammableGLDeviceTest`. | 4–6 h |
| **W1-2 One ccache config** | `scons/ccache.py enable()`: when the managed cache is used (no explicit `CCACHE_DIR`), write `<cache>/ccache.conf` via `write_if_changed` with `max_size = 20G`, `compiler_check = content`, `compression = true`; delete the 4G default; `CCACHE_MAXSIZE` from the environment still overrides. `tools/dev_build.py`: drop the `CCACHE_MAXSIZE` setdefault. `scons/dev_store.py paths()`: report the real limit/size by parsing `ccache --show-config`/`--print-stats`. `scons/dev_build.py cache_flags()`: apply `CCACHE_BASEDIR` \+ `-fdebug-prefix-map=<root>=.` to every identity except shipping invocations (`bundle`/`package`/`install`/`retain_symbols` in `COMMAND_LINE_TARGETS`) so dSYM paths for packages are untouched while opt builds become relocatable. Update `test/test-ccache.py`, `build-options.md`, `development-storage.md`, AGENTS.md "12 GiB ceiling" text. | Ends the 5,927-cleanup thrash (11 GB working set vs 4/5/12G limits); dev-fast glob2 is \~0.1 GB compressed so 20G covers release variants | Release objects get relative debug paths: document `lldb settings set target.source-map . <root>`. Verify `ccache -s` cleanups stop growing over a week. | 2 h |
| **W1-3 Fix the spurious second-build recompile** | Two causes visible in `--debug=explain`: (1) 749 C++ objects report "`src/X.cpp` is no longer a dependency / is a new dependency" with identical text — the cold build recorded one node form (variant `build/.../src/X.cpp` vs srcnode `src/X.cpp` under `duplicate=0`) and the second graph holds the other; (2) 43 C objects (quickjs/openlibm) report "build action changed" — `scons/build_timing.py record()` adds Python Pre/PostActions only when `GLOB2_BUILD_TIMING_LOG` is set, and function-action bytecode enters the action signature, so toggling the variable rebuilds every recorded node. Confirm with `sconsign -d … -e UnitStats.o -r .sconsign.dblite` before/after (compare `bsources` path strings) and by toggling the timing variable. Fix: in `src/SConscript`, `libs/gag/src/SConscript`, `libs/usl/src/SConscript` build one `nodes = {s: local.File('#src/'+s)}` map and pass nodes (never strings) to `development_objects`, `Program`, every harness list and `client_object_by_source`; delete the `local.Object(source)[0]` fallback in `client_objects`; if the variant/srcnode flip is intrinsic to `variant_dir` with `duplicate=0`, drop `variant_dir` for `src`, `libs/*`, `test` (object targets are already explicit paths). Move `build_timing.record()` to an `env['SPAWN']` wrapper (spawn is not part of signatures). | Removes the 41–44 s re-run after cold builds/branch switches, which is a full \~1,680 s rebuild whenever ccache misses | New `test/test-second-build.py` (pattern `test/test-dev-compile.py`): cold build, then `scons -n --debug=explain` prints zero `rebuilding`; `tools/benchmark_build.py` no-change case. | 4–8 h |
| **W1-4 Test provenance** (shared with W3-E1) | see W3-E1 | no relink per commit |  | 0.5 d |
| **W1-5 Machine-wide CPU token pool** | New `scons/jobserver.py` \+ `test/test-jobserver.py`. Tokens are `N = ncpu + 1` files `home()/state/jobserver/token-NN`; acquire \= `flock(LOCK_EX|LOCK_NB)` sweep in random order then blocking `LOCK_EX` on one token; release \= close (the kernel releases on crash — no leaked counts, which is why not POSIX semaphores or a bare fifo; Windows uses the `LockFileEx` path already in `dev_store.Lease`). SCons (`SConstruct main()`, `web_build.py`, `mobile_build.py`): an `env['SPAWN']` wrapper acquires a token around each spawned compiler/linker process — no wrapper executable, so direct ccache hits (0.04 s) pay no interpreter start; `-j` stays `ncpu`, tokens throttle real work. Asset export (`tools/package_assets.py encoding_executor()`): each `ProcessPoolExecutor` worker holds one token for its lifetime. Dependency builds (`shared_dependencies.ensure()`, `sdl3/recording/opus_dependencies.py`): `reserve(k)` takes `k = min(requested, free, ≥1)` tokens and exposes them to `make`/`cmake --build` via a private fifo (`MAKEFLAGS=--jobserver-auth=fifo:<path>`, make ≥4.4, ninja ≥1.12) — this bridge also serves W1-12. `test/run_tests.py:637,702`: each job holds a token while running. Lane registry `home()/state/lanes/<pid>.json` {worktree, builddir, start, argv} written in `SConstruct main()`, removed `atexit`, stale when pid dead; `dev_environment.py lanes` lists them. Interim PR first (2 h): `tools/dev_build.py` uses `-j = max(1, ncpu // live_lanes)` when `GLOB2_JOBSERVER=0`. | Removes the 40% CPU-per-job inflation (97 → 137 CPU-s), 3.5 GB swap and load 30–68 at 4 lanes; aggregate throughput back to \~1-lane efficiency (1.62 vs 1.07 lane-builds/min) | Deadlock if a token holder waits on a token-needing child: never hold a token while spawning a token user. Test spawns 24 sleepers, asserts max concurrency ≤ N and recovery after `kill -9`; rerun the 4-lane libgag test. | 1.5–2 d (+2 h interim) |
| **W1-6 Shared runtime-asset export** | `scons/runtime_assets.py prepare_assets()`, `tools/package_assets.py` (`export_assets`, default `cache`), `scons/web_build.py:119-150`, `platforms/mobile/android.py:194`, `ios.py:172`, `scons/dev_store.py` (add `clone_tree()`, `caches/runtime-assets`). Key \= `dev_store.key(root, inputs, extra)` where `inputs` is today's stamp list (`source_files(root, platform)` \+ tool scripts) and `extra = [platform, lossy, packing, video, encoder python version]`; use `git ls-files -s content/` blob ids plus hashes of dirty/untracked files (as `test/build_provenance.py` does) to avoid re-reading 620 MB. Entry `caches/runtime-assets/<key>/{tree, audit.json, .complete}` published like `shared_dependencies.ensure` (build in temp, verify, rename under exclusive `Lease`; readers hold shared leases). `clone_tree()` into `BUILDDIR/runtime-assets`: macOS `clonefile(2)` via `ctypes`; Linux/Windows per-file `os.link` on the same filesystem, else `copytree`. Store files chmod 0444\. The audit JSON stays beside `BUILDDIR/runtime-assets` so `install_export()` is unchanged. Also move `build/asset-cache` into the store (`tools/package_assets.py:763` default → `dev_store.cache(root,'asset-cache')`, hits `os.utime`, `prune` evicts by mtime). | \~240 s wall / \~800 CPU-s per fresh or asset-affecting rebase; 620 MB per worktree (62,036 copies, 0 hardlinks today); asset-cache 2.78 GiB per checkout → shared | Audit JSON byte-identical between in-place and cloned export; `RuntimePackCheck`, `test/test_mobile_asset_bundle.py`; disk delta per worktree. | 1–1.5 d (+0.5 d asset-cache) |
| **W1-7 Prebuilt master baseline** | New `tools/dev_baseline.py`; `scons/dev_store.py` (`baselines/`). Producer is the developer machine itself (hosted macOS objects do not match local Apple clang under `compiler_check=content`; Linux objects differ anyway): `dev_baseline.py build [sha]` checks out `sha` in a dedicated detached worktree `home()/baselines/worktree`, runs the dev\_fast build \+ `engine-tests` and the `perf=1` build (W3-A), then moves `build/` to `home()/baselines/<platform>-<toolchain digest>/<sha>/` with `baseline.json` (sha, identity, flags, ccache stats) and `.complete`; the warm ccache is a side effect. Triggered by a `launchd`/`/loop` job or `gh run watch` post-merge; CI only publishes the master SHA list. Leases: shared `Lease` while a consumer runs A/B; GC keeps the last 3 SHAs plus leased ones. Consumers: `dev_baseline.py path --sha <sha> --variant perf` returns the binary for benchmark scripts (its `BuildPaths.h` points at the persistent baseline worktree); `dev_baseline.py seed <worktree>` clones the dev\_fast `build/<id>` incl. `.sconsign.dblite` via `clone_tree()` and verifies with `scons -n --debug=explain` that only changed sources rebuild (this check is the gate for an unverified assumption). `tools/perf_ab.py --baseline master` (W3-G) resolves the merge-base. Depends on W1-1, W1-3, W1-6. | Replaces duplicate baseline worktrees (perf-master/bgwindows-base held 10.7 GiB); 16% of perf PRs rebuilt master binaries; new worktree ≈40 s of direct hits | Verify golden/checksum of the baseline binary equals a local build of the same SHA. | 2 d |
| **W1-8 `compile_profile=core`** (3 PRs) | **G1 manifest split**: `scons/sources.py` `CLIENT_SOURCES = CORE + GENERATOR_SOURCES` (69 `map/generator/generators/`) `+ AI_SOURCES` by id (castor, cortex, numbi, cabino; nicowar/econo/warrush stay core for the golden) `+ EDITOR_SOURCES` (40 `map/editor/` \+ `Game_editor.cpp`); new `test/test_sources_manifest.py` exact-cover test (union equal, pairwise disjoint). **G2 generated registries \+ marker**: new `scons/compile_profile.py`; `src/map/generator/core/GeneratorRegistry.cpp:169-240` → generated `BUILDDIR/generated/GeneratorBuiltins.cpp` from a JSON table preserving catalog order; `src/ai/AI.cpp:44-80, 303-350` → table `{id, create, load}` with null entries giving a clear "not compiled into this profile; rebuild with with\_ai=" error (saves/replays keep \`P\_AI+id\`); \`profile.json\` beside \`identity.json\` (not inside: \`prepare\_directory\` would refuse the shared dir) plus a generated \`CompileProfile.cpp\` string shown in \`--version\`; \`src/online/OnlineMatch.cpp:506\`, \`RoomSetup.cpp\` refuse agent/verifier/ladder/rated roles unless full; \`test/tests.py\` \`require={'generators','editor','ai:castor'}\` honoured in \`test/SConscript available()\`; \`AI::SIZE\` loops (BasePlayer, SGSL Parser/StoryActions, AINames, AITelemetry, EngineTiming, CustomGamePreferences) check availability. Options \`compile\_profile=core | full with\_ai=… with\_generators=… with\_editor=0`in`SConstruct establish\_options`. Core keeps all sim incl. JS/sgsl, HUD/UI, renderers, recording, online, Maxima+Nicowar+Econo+Warrush+JS AIs, generator core/shared,` lava-shield`(the`defaultMethod()`),` uniform`, one natural landscape. **Same BUILDDIR, no new defines.** **G3 stale includes**: drop` AICastor.h`/`AINicowar.h`from`src/render/GameRender\*.cpp`(5),`Game\_editor.cpp`,` Game\_io.cpp`,` Game\_sync.cpp`; move Cortex hooks in` Headless.cpp`,` Glob2.cpp\` behind the table. | Game.h edit 365 → \~260 TUs (−29%) core; \~226 (−38%) without editor; \~190 (−48%) Maxima-only \[E\]; clean −13% TUs / −19% weight | Full profile produces byte-identical objects and golden `FourSquares1.g2mr`; core refuses rated roles; a save with an excluded AI gives the clear error. "Works in my profile": CI always full; `dev_build.py check-full`; AGENTS.md requires full-profile verification of the final revision. `SIM_REVISION` unchanged. |
| **W1-9 Disk tooling** | **H1** `tools/dev_environment.py worktrees` (reuse `checkouts()`, `busy()`): per worktree size (`store.size`), last activity (newest tracked mtime, HEAD date, build mtime), branch, PR state via `gh pr list --head <branch> --state all --json state,mergedAt` (reports squash merges that `git branch --merged` misses), `.build-lock`/`git worktree lock`/lsof busy flags, flags `/private/tmp` and `.claude/worktrees` scratch; `remove` requires confirmation and a clean tree or `--discard` (1 d). **H2** build-dir GC: `SConstruct main()` touches `BUILDDIR/.last-use`; `dev_environment.py builds --gc --idle 14d` honours `.build-lock` (0.5 d). **H4** new `tools/check_artifacts.py`: no `.ppm/.bmp`, no `build/` under `artifacts/`, gzip logs \>1 MB, failing-only screenshots in `run_tests.py`; called from `disk` (1 d). **H5** `dev_store.require_free_space()` in `SConstruct main()`, `run_tests.py`, `package_assets`; default 10 GiB, `GLOB2_MIN_FREE_GIB` (2 h). **H6** `SConstruct:363`: for optimized non-shipping invocations replace `-g` with `-gline-tables-only` (clang) / `-g1` (gcc) and add `-dead_strip`; verify `.text` hash equal (2 h). **H7** `platforms/mobile/android.py`, `scons/mobile_artifacts.py:99`: hardlink/clonefile the `libmain.so` staging, one `.dbg` per arch (3 h). **H8** `dev_environment.py prune --toolchains`: remove `toolchains/*` and `caches/emscripten-*` whose key matches no registered checkout manifest; report the Homebrew SDK (3 h). **H9** git evidence expiry, one-off, documented in `development-storage.md`: delete `refs/heads/evidence/*`, `refs/codex/snapshots/*`, `refs/codex/turn-diffs/*`, `refs/archive/*`, `refs/tags/{codex,archive}/*` locally and on origin; `git fetch --prune`; `git reflog expire --expire=now --all`; `git repack -a -d -f --window=250 --write-bitmap-index`; `git prune` (worktree HEADs survive) (1 h \+ repack). **H10** `dev_environment.py disk`: ranks worktrees, builds, store, `.git`, artifacts, external caches (parallel `scandir`, cached) — replaces the 5-min `status` (0.5 d). | \~38 GiB worktrees now (+\~4/lane later); \~15 git; 10–14 artifacts; 6–9 debug info (opt-tests 4.85 → \~1.2); 5–10 GC; 13–17 toolchains; 2.2 Android | Worktree removal and ref expiry are irreversible: confirmation prompts; never touch locked/dirty trees; expire refs only after the maintainer confirms (decision taken: yes). | 3–4 d total |
| **W1-10 PCH expansion for dev\_fast** | `scons/dev_compile.py HEADERS` (today `algorithm array cstddef cstdint map memory optional span string vector`): add `chrono functional iostream filesystem format`, `nlohmann/json.hpp`, `SDL3/SDL.h` (CPPPATH already in the PCH command). Trade-off: PCH TUs bypass ccache, so after W1-1 a second worktree loses its 41 CPU-s warm path and pays a cold compile. Decision rule measured with `tools/benchmark_build.py` on one lane: make `pch=1` the dev\_fast default only if cold CPU ≤0.75× and header-edit rebuild ≤0.7×; otherwise keep opt-in and document "use `pch=1` for header-heavy iteration in one worktree". `pch_defines,time_macros` sloppiness becomes possible once `__DATE__` is gone (W1-1). | libc++ is \~1,000 of the 1,317 files a median TU pulls in; attacks the 94%-frontend cost | Measured decision; no correctness risk. | 0.5 d \+ measurement |
| **W1-11 SCons tuning** | `SConstruct main()`: `SetOption('implicit_cache', True)` for dev identities (document `--implicit-deps-changed` after include-path moves); `Decider('MD5-timestamp')`; `scons/test_provenance.py`: compute `source_identity()` inside the Command action (`AlwaysBuild` \+ `write_if_changed`) instead of at every read (part of the 7.1 s read) — superseded if W3-E1 lands first; order `INCLUDE_DIRECTORIES` by hit frequency and prune entries W2 makes unnecessary. | No-op 27.7 s → \~10 s \[E\] | `tools/benchmark_build.py`; touch test confirms no missed rebuilds. | 0.5 d |
| **W1-12 CMake \+ Ninja migration (late)** | Order: (1) `libusl`, `libgag`, quickjs/openlibm with `numeric_guard` as a post-build check; (2) client objects \+ `glob2`, sources from `scons/sources.py` via a generated `sources.cmake` with a CI sync check; (3) generated headers; (4) `test/tests.py` binaries; (5) harnesses; (6) runtime assets (calls W1-6's shared export); (7) packaging; (8) relay; (9) web and mobile last. Builder mapping: `ccache.py` → `CMAKE_<LANG>_COMPILER_LAUNCHER=ccache`; `build_layout.py` identity/dir/lock → `CMakePresets.json` per identity, `identity.json` kept; `BuildConfig.h`/`BuildPaths.h`, `skin_materials.py`, `hive_api.py`, `online_error_catalog.py`, `skin_render.py`, `test_provenance.py` → `add_custom_command` running the same Python; `renderer_shaders.py` → custom command \+ `install(DIRECTORY)`; `runtime_assets.py` → custom target with stamp; `dev_compile.py` PCH/unity → `target_precompile_headers`, `UNITY_BUILD`; `javascript.py` strict flags → `set_source_files_properties(COMPILE_OPTIONS)`; `bundle.py`, `dmg.py`, `nsis.py`, `install_image_runtime.py` → `add_custom_target` (CPack later); `shared_dependencies.ensure` → configure-time `execute_process`; `web_build.py`, `mobile_build.py` → emscripten/NDK toolchain files; `build_timing.py` \+ jobserver SPAWN → Ninja `-d stats` \+ the `MAKEFLAGS` fifo bridge from W1-5. Parallel operation: `tools/dev_build.py --generator=cmake` selects CMake, SCons stays default; CI job `build-system-equivalence` (Linux GCC 13, later macOS) builds both with `-ffile-prefix-map=<root>=.` and compares `compile_commands.json` argument multisets and `sha256` of every stripped `.o`; link-order differences checked by the headless replay checksum. SCons is removed when all workflows and packaging run on CMake and equivalence has been green for four weeks; `scons/*.py` modules CMake still calls move to `buildtools/`. | No-op \<1 s, native jobserver, `-MD` deps, IDE integration | Divergence risk → one exported source list, object parity in CI, mobile/web last. Start only after W1-1..W1-7 and W2 Phase A/B. | ≈12 engineering days over ≥8 PRs (3–6 w elapsed) |

Phase order inside W1: W1-2, W1-1, W1-3, W1-11 (week 1, hours each) → W1-5 (interim then full), W1-6, W1-9 H1/H5/H10/H6/H9 (weeks 1–2) → W1-7, W1-8 (3 PRs), W1-9 H2/H3/H4/H7/H8, W1-10 measurement (weeks 3–4) → W1-12 (later; needs W1-1, W1-3, W1-6, W1-5 fifo bridge).

---

## **7\. Workstream 2 — header decoupling, layering, breaking the tangle**

Common verification for **every** PR here: `SIM_REVISION` unchanged and `test/check_sim_revision.py --base` passes with the committed `match verify` trace byte-identical; headless replay checksums of a fixed replay set identical on macOS arm64 \+ Linux x86-64 (`docs/development/headless-replays.md`); save round-trip (`test/compare_save_continuation.py`, SaveSizeHarness) where serialization code moves; for perf-sensitive steps, release build \+ `otool -tV`/`objdump -d` on `Unit*.o Building*.o MapGradient*.o MapPathfind*.o MapStep.o` with `grep -c 'call.*\(availableMaterial\|coordToIndex\|terrainPropertiesAt\|getBuilding\|getGroundUnit\|computeExecutor\|routing\)'` \= 0 in per-cell kernels; `tools/gradient_benchmark.py --suite representative --repeats 11` within noise; rerun the include-graph fan-in script and report before→after in the PR.

### **Phase A — cheap, near-zero risk (≈1 week total)**

**W2-A1 Version constants out of headers.** Audit: `grep -rnE '=\s*(VERSION_[A-Z_]+|SIM_REVISION|MINIMUM_VERSION_MINOR|REPLAY_MINIMUM_VERSION_MINOR|FILE_FORMAT_VERSION_[A-Z0-9_]+)\b' --include='*.h' src libs test`. Current hits: `src/team/stats/TeamStat.h:434` (`loadSample(..., int versionMinor = VERSION_MINOR, ...)`), `src/building/HiringPipeline.h:62`, `src/team/stats/TeamStatsPipeline.h:53`, `src/scripting/javascript/ScriptObservations.h:113`, `AIMaximaContinuation.h:251` (ctor default), `src/online/SimVersion.h:83` (`simRevisionEntry(int = SIM_REVISION)`), `GradientRuntime.h:38` (default member initializer), `SaveLoadProfile.h:12` (inline constexpr); 17 headers include the version headers (MapHeader.h, ReplayReader.h, AIStateSerialization.h, OrderSelection.h, ResourceInitializationCache.h, LegacyPopulationOutput.h, GameGUIStubs.h…). Mechanics: replace each default argument with an overload whose one-arg form is defined in the `.cpp` and forwards the constant; `hasAdaptiveGradientState` becomes a declared function in `SaveLoadProfile.cpp`; `GradientRuntime::loadingGradientVersion` initialised in the ctor; move includes to `.cpp`, declare a function when a header needs a constant. Fan-in: Version.h 499→54, FileFormatVersions.h 504→162, SimRevision.h 111→2 \[E\]; −8.1% recompiles (TeamStat edge alone −4.3%). Perf none (cold load paths). 0.5 d.

**W2-G1 CI guard `test/check_header_layering.py`** (land right after A1; pattern: `test/check_sim_revision.py`; wire into the cheap-contracts step and the fast gate; unit tests `test/test_check_header_layering.py`). Pure-Python textual include graph resolved against `INCLUDE_DIRECTORIES` from `scons/sources.py`; runs in seconds. Rules: (1) layer order from `test/header_layers.json` (gag-core 0; common 1; resource, field, `*/types` 2; map 3; unit, building, team 4; game, engine/sim 5; ai, scripting 6; render, hud, ui, net, online, replay 7; app 8\) — a header may include only ranks ≤ its own; current violations live in `test/header_layering_allowlist.txt`, new ones fail, PRs delete lines; (2) sim headers (map, unit, building, team, game, engine/sim, resource, field) never include `src/render`, `src/hud`, `src/ui`, `SDLGraphicContext.h`, `GAGSys.h`, `<SDL3/SDL.h>` (`SDL_stdinc.h`, `SDL_iostream.h` allowed); (3) no `= VERSION_*`/`= SIM_REVISION` defaults in headers (zero tolerance); (4) component cycles: directory-level graph at depth 1 and 2, Tarjan SCC, any SCC edge not in `test/header_cycles_allowlist.txt` (seeded with today's 119 edges) fails; (5) fan-in budget `test/header_fanin_budget.json` for \~25 culprit headers, `--write` lowers budgets after a PR (ratchet; never raised without review). 1–2 d.

**W2-A2 `GAGColor.h` leaf; `GraphicContext.h` stops re-exporting; SDL umbrella out of type headers.** New `libs/gag/include/GAGColor.h` with `GAGCore::Color`/`Color32` moved verbatim from `SDLGraphicContext.h:54-96`, including only `<SDL3/SDL_stdinc.h>`. `GraphicContext.h` becomes forward declarations (`class GraphicContext; class DrawableSurface; class Sprite; class Font; class RenderBatch;`) \+ `GAGColor.h`. Includers audited: Color-only — `GameEvent.h`, `BaseTeam.h`, `Brush.h`, `TeamStatChart.h`, `SceneEntities.h`, `GameGUIDialog.h`; pointer-only — `Minimap.h`, `MarkManager.h`, `OverlayAreas.h`, `MapAssetSprites.h`; need SDL event/rect only — `UIService.h`, `ConnectionOverlay.h`; need the full class (include `SDLGraphicContext.h` explicitly) — `GUIBase.h`, `GUIStyle.h`, `RenderBatch.h`, `TrueTypeFont.h`, `ui/Theme.h`, `GUIMapPreview.h`, `TerrainCompositor.h`, `SoftwareTerrainCache.h`, `RenderStateScope.h`, `HostViewport.h`; \~59 `.cpp` includers fixed compile-error-driven. `UnitConsts.h`, `UnitType.h`, `BuildingType.h`, `Unit.h`, `Ressource.h`: `GAGSys.h`/`<SDL3/SDL.h>` → `<SDL3/SDL_stdinc.h>` (check no `MAX/MIN/VARARRAY` macro use). Fan-in: SDLGraphicContext.h 549→228, GAGSys.h 603→338, BuildConfig.h 608→347, AssetLoader.h 556→239 \[E\]; −10.2% recompiles. Must compile on Windows/mobile/web (`ci:full`). 1.5–2 d.

**W2-A3 `json_fwd`, `MapAssetBundle` forward declaration, AI-id leaf.** `WorldSnapshot.h:16` → `class MapAssetBundle;` (`shared_ptr<const MapAssetBundle>` needs no complete type); `PlatformProtocol.h:6` → `<nlohmann/json_fwd.hpp>`. New `src/ai/AIImplementationId.h` with the enum moved verbatim; `AI.h` keeps `using ImplementationID = ::AIImplementationID; using enum ::AIImplementationID;` so `AI::NONE` etc. still compile; `BasePlayer.h` includes the leaf. json.hpp 472→143, AI.h 446→118 \[E\]; Scene.h no longer reaches AI.h. Serialized values untouched. 0.5 d.

**W2-A4 `Map.h` drops `Building.h`/`Team.h`; `Building.h` drops `MapInternal.h`; constant-only `Team.h` users → `TeamCapacity.h`.** New leaf `src/building/BuildingRoute.h` (enum \+ count from `Building.h:64-65`); new leaf `src/team/stats/CoverageBuilding.h` (`TeamStats::CoverageBuilding` kept as alias); `TeamCapacity.h` gains `inline constexpr Uint32 teamNumberToMask(int)` (Team's static forwards). In `Map.h`: `Team::MAX_COUNT` → `TEAM_MAX_COUNT` (\~20 array dimensions), `Team::teamNumberToMask` → free function, forward-declare `Team`, `Building`; any inline body dereferencing them (4 of 38 mentions) moves to `.cpp` (cold; none of the hot accessors touch Building/Team members). `Building.h`: `MapInternal.h` → `field/GradientConstants.h` (needs only `MOVEMENT_CLASS_COUNT`). Constant-only includers (`ResourcePlaneKey.h`, `SnapshotGradient.h`, `OutcomeReason.h`, `MapHeader.h`, `LegacyGenerationDescriptor.h`, `GenerationContext.h`, `MapGeneratorToolkitChecks.h`, `AIWarrushTuning.h`, Cortex\*/Maxima\* headers) → `TeamCapacity.h`. Team.h 693→621. Enabler for B/C. 1 d.

**W2-A5 `Building.h` drops `Team.h` while `availableMaterial` stays inline and as fast.** `availableMaterial` (`Building.h:263`) and `materialNeed` (:340) read `owner->reservedTeamMaterials[r]`. Add `const Sint32* ownerReservedTeamMaterials = nullptr;` beside `Team *owner` (:591), set wherever `owner` is assigned (both ctors, `load`, `loadCrossRef`); bodies become `type->useTeamMaterials ? ownerReservedTeamMaterials[r] : reservedMaterials[r]` (one load fewer than today; `Team` is heap-allocated in `Game::teams` and outlives its buildings; not serialized, not checksummed). Then forward-declare `Team`. Rejected alternative: a `Building-inl.h` (a TU that forgets it would silently get a non-inlined copy under non-LTO). Verify: call-instruction count for `availableMaterial`/`materialNeed` in release `Unit*/Building*/HiringPipeline` objects \= 0; checksums identical. 0.5 d.

### **Phase B — medium (≈2 weeks)**

**W2-B1 Render out of `Game.h` → `src/render/MapDrawer.h`.** `Game.h:400-534` holds \~35 `static void draw*`/`anchorBars`/`isOnScreen`; `Game::ViewState` (:596-624) embeds `MapRenderState` by value, which is why `Game.h:33` includes `render/MapRenderState.h → Scene.h → WorldSnapshot.h`. Move the statics into `namespace MapDrawer` (`GameRender*.cpp` definitions renamed; they take `ViewState&`, `PresentationFrame`, `SceneMap`; the three private statics reached via friends — `PointBarRenderTest`, `EnteringUnitDrawHarness`, `FailingUnitMarkersHarness`, `HighResolutionIntegrationHarness` — are updated); `ViewState` → `src/render/ViewState.h` (owned by GameGUI/MapEdit as today; keep `Game::ViewState` alias for one release). `Game.h` drops the render include. Scene.h 371→93, MapRenderState 367→77 \[E\]; −4.7% churn-weighted; makes the "drawing reads only Scene" rule true for headers, not just data. Verify render harnesses and `test/run_software_render_comparison.py`. 2–3 d.

**W2-B2 Pimpl `ComputeExecutor` in `Map` and `SnapshotStore` in `Game`.** `Map.h:118` `mutable ComputeExecutor compute;` → `std::unique_ptr<ComputeExecutor>` constructed in the Map ctor; `computeExecutor()` derefs (used per job/dispatch, never per cell — confirm with `grep -rn 'computeExecutor()' src/map` that no call sits in a per-cell loop). `Game.h:297` `mutable SimulationSnapshot::Store worldSnapshots;` → `unique_ptr`; `snapshots()`/`snapshotStore()` deref. ComputeExecutor.h 414→\~40 \[E\]; −2%. Neither is serialized. Verify gradient\_benchmark. 0.5–1 d.

**W2-B3 Leaf moves, `Fwd.h` headers, stop re-exporting.** Move `src/game/ExperimentalFeatures.*` → `src/common/`, `src/game/Sector.*` → `src/map/` (include paths are flat, no includer edits) — removes the two map→game edges. Add `MapFwd.h`, `TeamFwd.h`, `BuildingFwd.h` (includes `BuildingRoute.h`), `GameFwd.h`; convert pointer-only includers (`AIRuleOrders.h`, `AIRules.h`, `AIStateSerialization.h`, `BuildingDemands.h`, `CortexBuildings.h`, `LegacyPopulationOutput.h`, `OutcomeReason.h`, `ScriptBuildingCapabilities.h`, `TorusMapFixture.h`, `Farmland.h`, `GenerationFertilityWork.h`, `Gradients.h`, `Pipeline.h`, `Planting.h`, `ResourceSemantics.h`, `SetLibraryDialog.h`); delete stale includes (`AICabino.h`, `AIMaximaBuildings.h`, `MaximaObservationBinding.h`, `Minimap.h`, `SaveSnapshot.h`, `AIMaximaRuntime.h`, `Management.h`; `AICastor.h`/`AINicowar.h` in `GameRender*`/`Game_*.cpp`). `Minimap.h` drops `Game.h`; `GameGUI.h` stops re-exporting Brush/Campaign/Minimap/SceneExtract; `Engine.h` holds `std::unique_ptr<GameGUI>` with `gui()` so its 53 includers stop pulling GameGUI.h+Game.h (`engine.gui.` → `engine.gui().`). ≈−2.2%; Game.h 553→511, Map.h 599→431 (incl. tests). 1–2 d.

**W2-B4 Terrain built-ins → registry data behind a narrow header.** Keep `enum TerrainType : uint16_t` (205 TUs name enumerators). Move the 25 `TERRAIN_COUNT_BEFORE_*` format-history constants (`TerrainType.h:115-151`) to `src/map/io/TerrainFormatHistory.h` (TerrainRegistry.cpp \+ tests only). `TerrainTypeTable.h` constexpr rows (names, colours, groups, paintable, legacy names) → data in `TerrainRegistry.cpp` behind `TerrainCatalog.h` (`terrainTypeDefinition(TerrainType)`, `terrainTypeByName`); if any per-cell rule uses `terrainGroup()` (check `CellRules.h`) keep a tiny constexpr group array in `TerrainGroupTable.h`; `terrainPropertiesAt` untouched. Same-PR equivalence test iterating all ids against the old table. Kills the add-a-terrain cluster for table edits (enum additions still rebuild enum includers — accepted). 1–2 d.

**W2-B5 Split `test/support/EngineFixtures.h`** into `HeadlessGlobals.h`, `HeadlessGame.h` (Game+GameGUI), `FixtureUtilities.h`; EngineFixtures.h becomes an umbrella and is removed once the 172 includers use the narrowest header. Prepares C6. 0.5–1 d.

### **Phase C — deep (≈6–10 weeks, parallelisable)**

**W2-C1 `MapRouting` behind `unique_ptr` (`map.routing()`).** `Map.h:941-1266` routing API and the gradient arrays at :1367-1421 (`materialGradients[TEAM][Material][Movement]`, forbidden/guard/clear gradients, routing cache, building gradient leases) → `class MapRouting` in `src/map/pathfind/MapRouting.h`; `Map { unique_ptr<MapRouting> routing_; MapRouting& routing(); }`. `MapRouting.cpp` includes `Map.h` (hot accessors stay inline inside its kernels); the header forward-declares. Per-unit call sites (`pathfindBuilding`, `buildingAvailable`) pay one pointer load per query (measured within noise); per-cell loops stay inside. Save/load of routing cache and gradients moves with identical stream order; `Map::checkSum` keeps its traversal. Bound ≤−4.2% (82 of 595 Map.h includers call routing). 1–2 w.

**W2-C2 `SimContext` in `Team` (replaces `owner->game->X`).** Measured uses in team/building/unit `.cpp`: `gameHeader` 35, `areaEffects` 23, `buildingsTypes` 21, `stepCounter` 20, `map` 17, `unitCargo` 16, `teams` 16, `animations` 9, \~10 rare calls (`publishClientEvent`, `allocateScriptIdentity`, `resolveUnit`, `hiringCandidates`, `turretTargeting`). `src/engine/sim/SimContext.h`: plain pointers for hot data (`const GameHeader*`, `Map*`, `BuildingsTypes*`, `UnitCargoStore*`, `BuildingAreaEffects::Runtime*`, `const Uint32* stepCounter`, `Team* const* teams`) \+ a small `SimServices` abstract interface for the rare per-tick calls, implemented by `Game`. `Team` gets `SimContext ctx` set in `Team(Game*)`; Team/Building/Unit `.cpp` include `SimContext.h` instead of `Game.h`. Same load count as today. Also removes the `Construction→TeamStep→Game_sync` link edge. 1–2 w.

**W2-C3 `GenerationWorld` narrow API for generators.** `GenerationContext.h` includes `Team.h`; 100 generator TUs include `Map.h`/`Game.h`. `src/map/generator/core/GenerationWorld.h`: terrain get/set, resource placement, team/start-position and building/unit *descriptors*, RNG, `TEAM_MAX_COUNT`; a `GenerationService` applies descriptors to Map/Game in one TU. Step 1 is a thin forwarding wrapper so generated maps are byte-identical (verified by `MapGeneratorGoldenTest` / `test/map-generator-golden.txt` and resource-epoch JSON). Generators then rebuild only on `GenerationWorld.h` changes. 1–2 w; parallel with C1/C2.

**W2-C4a Welding edges: replay, hiring drain, diagnostics.** `ReplayWriter::init(backend, Game&, std::function<void(OutputStream*)> writeHeader)`; GameGUI passes `[&](auto* s){ save(s, "replayHeader"); }` (same bytes). `ReplayReader.cpp:69` `GameGUI tempGui; tempGui.load()` → injected `HeaderSkipper` callback implemented in hud and registered by the app shell. `Map.cpp:519,527` `Game::drainHiring/drainTeamStats` → `Map::setComputeReconfigureHook(std::function<void()>)` set by `Game::setMap` (same call sequence). `MapRender::toPng` (`MapRender.cpp:66-92`) → `src/render/preview`; `Diagnostics::ImageRenderer` interface registered at startup; headless builds get a null renderer that errors "offscreen renderer unavailable". 2–3 d.

**W2-C4b AI factory → generated registry; JS idle hook.** `AI.cpp:44-74, 303+` `switch (implementationID)` → `AIRegistry::factories[AI::SIZE]` filled by the generated TU from W1-8 (deterministic; no self-registration so no `-force_load`/`--whole-archive` needed for AIs; unknown id → clear error; `P_AI` ids unchanged). `GameAI.cpp:164-169` `static_cast<AIJavaScript*>` idle loop → `virtual void AIImplementation::idle()`, so `GameAI.o` drops the QuickJS reference. 2–3 d.

**W2-C5 `globalContainer` out of sim TUs (ratchet).** Sim code uses `game->buildingsTypes` (already the frozen per-game registry) and `SimContext`; a guard counts `globalContainer` references under map/unit/building/team/game/engine/sim and may only decrease (budget file as in G1 rule 5). 1–2 w incremental.

**W2-C6 `HeadlessSim`, `libglob2_map.a` / `libglob2_sim.a`, per-module tests.** `scons/sources.py`: `CLIENT_SOURCES = MAP_SOURCES + SIM_SOURCES + AI_SOURCES + RENDER_SOURCES + HUD_SOURCES + UI_SOURCES + NET_SOURCES + APP_SOURCES` (consumers `mobile_build.py`, `web_build.py`, `dev_build.py` unchanged; exact-cover test). `src/SConscript`: `StaticLibrary` per group; the game keeps loose objects initially. `test/SConscript`: `glob2-map-tests` (libglob2\_map \+ gag core) and `glob2-sim-tests` (map \+ sim \+ AI registry with AINull only), each with `test/support/HeadlessSim.h` (Game without GameGUI/GlobalContainer; step \= `Game::step` \+ client-event drain). Doctest: test TUs stay loose objects per program so self-registration is not archived; if any test object ever enters an archive use `-Wl,-force_load` (Apple) / `-Wl,--whole-archive` (GNU/MinGW); add a case-count contract in `run_tests.py` (union of `--list-test-cases` across binaries ≥ previous `glob2-engine-tests` inventory). 1–2 w after C1–C5.

### **W2 PR sequence**

| PR | Depends on | Effort | Main saving |
| :---- | :---- | :---- | :---- |
| A1 versions | — | 0.5 d | −8.1% recompiles |
| G1 guard | A1 | 1–2 d | prevents regression |
| A2 Color/GraphicContext/SDL | — | 1.5–2 d | −10.2%; SDLGraphicContext 549→228 |
| A3 json/AI-id | — | 0.5 d | json 472→143, AI.h 446→118 |
| A4 Map.h leafs | A3 | 1 d | enabler; Team.h 693→621 |
| A5 Building cached ptr | A4 | 0.5 d | Building.h drops Team.h |
| B1 MapDrawer | A2 | 2–3 d | −4.7%; Scene.h 371→93 |
| B2 pimpls | — | 1 d | −2% |
| B3 moves/Fwd/re-export | A4 | 1–2 d | −2.2% |
| B4 terrain data | — | 1–2 d | −1.1%, kills add-terrain cluster |
| B5 fixtures split | — | 1 d | test TU isolation |
| C1 MapRouting | A4, B2 | 1–2 w | ≤−4.2% |
| C2 SimContext | A5 | 1–2 w | team/building/unit off Game.h |
| C3 GenerationWorld | A4 | 1–2 w | 88 generator TUs off Map/Game |
| C4a/C4b weld edges | B1, B5, W1-8 registry | 1 w | SCC shrinks |
| C5 globalContainer ratchet | C2 | 1–2 w | sim link isolation |
| C6 libs \+ module tests | C1–C5 | 1–2 w | per-module test binaries |

A1+A2+A3 ≈ −24% recompiles / −21% cost; all of A+B ≈ −28.5% with no hot-path change; C adds the routing bound and link isolation.

---

## **8\. Workstream 3 — policy, CI, tests, performance workflow**

### **W3-0 Hygiene first**

- **H2 OpenSSL 4 fix** (an hour): open a PR with only the `LanIdentity.cpp` change (cherry-pick `4b38a8422c`; `fix/openssl4-lan-identity` may already be that commit). Master must build on the maintainer's Mac before anything else is measured.  
- **H1 Apple clang `-O2` vectorizer check** (0.5 d): commit the torus 3×3 kernel as `tools/compiler/torus_stencil_check.cpp` \+ `tools/compiler/check_stencil_vectorizer.py` (compile at O0/O2/O2 `-fno-vectorize` with ASan, diff sums); run in `ci-macos.yml` and the `linux-clang` job. Decision rule: if it reproduces on real `src/map/generator/shared/{HeightMap,Patterns,Terrain}.cpp` stencils, add `-fno-vectorize -fno-slp-vectorize` to the strict numeric clone at `src/SConscript:53` for clang and refresh macOS generator rows; if it is benchmark UB, record as benign in `build-options.md` and delete the check.

### **W3-A Build ladder: `perf=1` independent of `release=1` (0.5 d \+ 1 d validation)**

`SConstruct:40`: `BoolVariable("perf", ...)` and `opts.Add("opt", "2|3", "2")`; `scons/build_layout.py:88` mode `'perf'`, reject with `release`/`profile`/`dev_fast`/`pch`/`unity`; `default_directory` yields `build/<toolchain>/client/perf[/o3]`, flock'd like today. `SConstruct:515-521`: `if release or perf: -O<level>` (3 for release, `opt` for perf); `-s` and `-fwhole-program` stay release-only; perf keeps `-g1`; `SConstruct:359-364` strip and `size_optimization.py` stay release-only; `client_profile` stays `full` (already default unless release); document `sprite_packing=video optimized_assets=1` for rendering benchmarks that need shipped asset paths. `scons/dev_build.py:cache_flags` extends `CCACHE_BASEDIR`/`-fdebug-prefix-map` to perf. `tools/dev_build.py` accepts `perf=1` (no `dev_fast` injection). Verify: `scons perf=1 engine-tests` builds; golden trace equals dev\_fast (same-platform O0 vs O2 match); `scons release=1` unchanged. Validation: rebuild base/head of 3–5 past perf PRs (candidates: \#1194 cold ground routing, \#1406 amphibious gradients, \#857 engine snapshots, \#646 field kernels, \#1367 telemetry) at O2, O3, release on Linux; 7 alternating pairs each; accept O2 as screen tier if the sign of every ratio agrees; record in `build-options.md`.

Ladder (doc table):

| Tier | Command | Evidence it may back |
| :---- | :---- | :---- |
| dev | `tools/dev_build.py` (`-O0 -g1`) | correctness, checksum/golden traces, save/replay continuity |
| perf-screen | `scons perf=1` (`-O2`) | direction and rough size of a perf change |
| perf-confirm | `scons perf=1 opt=3` | the number quoted in a perf PR; required when screen shows \<5% or mixed signs |
| release | `scons release=1` \+ packaging | shipping, package size, store QA only; never PR perf evidence |

Docs to rewrite in step: `build-options.md:41-45`, `rendering-benchmarks.md`, `software-rendering-benchmarks.md`, `memory-benchmarks.md`, `gradient-benchmarks.md:222`, `compute-benchmarks.md:405`, `resource-benchmarks.md:153-163`, `simulation-verification.md:11`.

### **W3-B AGENTS.md rewrite (after A, C, D1, E have landed so the text is true)**

Replace "Documentation and temporary evidence", "Compatibility reminders", "Development and release builds", "Review and merge ownership", "Validation and CI feedback" (keep Repository map, Dependencies, Preserving feel, Evaluating AI changes). Draft:

> ## **Documentation and evidence**

> Commit documentation only when it has a long-term audience, a category in `docs/README.md`, and will be maintained with the behavior it describes. Prefer updating an existing guide. Never commit development artifacts: screenshots, dumps, benchmark reports, working notes or PR narratives. Generated files go under the ignored `artifacts/`, notes under `docs/.work/`.  
>   
> Evidence for a PR lives in its description: the commands run, result counts, checksums, a short before/after table, and at most a few screenshots. Anything that does not fit in the description is not kept; there are no evidence branches, artifact repositories or committed dumps. Summarize durable conclusions in the relevant guide instead. Broad AI experiments use the storage controls in `tools.tournaments` ([experiment storage](http://docs/tools/tournaments.md#experiment-storage)).

> ## **Compatibility reminders**

> - **Simulation path.** Code under `src/game/`, `src/unit/`, `src/building/`, `src/team/`, `src/resource/`, `src/map/` (including `gradient/`, `pathfind/`, `generator/`), `src/ai/`, `src/engine/sim/`, `src/replay/`, `src/net/turn/`, `src/scripting/`, `libs/usl/`, `third_party/quickjs-ng/`, the simulation data files listed by `deploy/sim_version.py`, and the strict numeric flags in `src/SConscript` (`-fno-fast-math -ffp-contract=off`) is the simulation path. Changing anything on it needs matching per-tick checksums for the same state, seed and orders on Linux and one other platform you can run locally (macOS or a browser build), using the dev build. A change to optimization flags or compilers instead needs a same-platform O0 vs O3 checksum match. Changes outside the path need no cross-platform run; say so in the PR. See [simulation verification](http://docs/development/simulation-verification.md).  
> - **Sim version.** `SIM_REVISION` in `src/game/SimRevision.h` is the only thing that ties online rooms, ratings and verifiers to simulation behavior. Bump it in every change that can alter what the simulation computes (rules, units, AI, order validation, map loading, random use, scripting), then run `python3 tools/refresh_sim_fixtures.py` in the same change; it regenerates every revision-keyed fixture and the fast gate fails when one is stale.  
> - **Saves** are durably compatible: `MINIMUM_VERSION_MINOR` in `src/app/Version.h` is the floor the loader must still read. A save-format change needs version-gated loading, and derived caches are not always safe to discard. Dropping save support is an explicit, called-out decision.  
> - **Replays and network** reset more readily and are a separate question from saves: check `src/replay/ReplayReader.h`, `src/app/Version.h` and `src/online/SimVersion.cpp` and test the acceptance boundaries you move.

> ## **Build tiers**

| Tier | Command | Use it for |
| :---- | :---- | :---- |
| dev | `python3 tools/dev_build.py` (`-O0 -g1`, ccache, all features; `compile_profile=core` for focused work) | editing, tests, checksum and golden traces, save/replay checks |
| perf-screen | `scons perf=1` (`-O2`, full profile, symbols) | first measurement of a performance change |
| perf-confirm | `scons perf=1 opt=3` | the number you quote in a perf PR |
| release | `scons release=1` and the platform packaging workflow | shipping and package verification only |

>   
> Never ship a dev or perf build, and never quote release measurements as PR evidence: perf builds exist so that every performance claim is reproducible from one `scons` command. Compare against the prebuilt master baseline (`tools/perf_ab.py --baseline master`) rather than a second worktree. Plain `scons` gives full debug information; `pch=1` and `unity=1` are development experiments. Simulation checksums must match between dev and perf builds of the same platform; do not bump the sim version to mask a build-induced mismatch. Verify the final revision of a PR with the full profile. Cache and storage limits: [build options](http://docs/development/build-options.md); `python3 tools/dev_environment.py disk|worktrees|gc` report and reclaim space.

> ## **Review and merge**

> The author prepares, verifies and merges their own PR, including core behavior and policy changes; independent review is optional. The PR description states the intended result, the commands run with their results, what was not covered and why. Visual or feel changes include a screenshot or a sentence on how it plays; a maintainer playing the result is part of review. Performance changes include the A/B table from `tools/perf_ab.py`.

> ## **Validation and CI**

> Merging waits for the required **fast gate** (`ci-fast.yml`, target under 15 minutes): cheap contracts, the sim-version check, header-layering contracts, a Linux dev build and the headless test tier. Everything else (full Linux matrix, Windows, macOS, Android, browsers, coverage) runs asynchronously on every master push and nightly; it is not a merge prerequisite. Add the `ci:full` label to run the full matrix on a PR when the change touches build inputs, shared headers, platform shells or test infrastructure.  
>   
> **Master red rule.** When the newest full master run has been failing for more than 24 hours, the next merge must be the repair or a revert; the gate's `master-health` job enforces this and is satisfied by the `ci:repair` label. Keep repair PRs focused.  
>   
> Locally, `python3 test/run_tests.py` runs the headless non-slow tier (about 15 minutes serial, a few minutes with `-j`); `--tier full` adds display and slow cases; `--affected` runs only cases whose recorded coverage intersects your diff. Choose focused checks from the change's actual risks, keep every affected compatibility boundary covered, and fetch current master before final validation without rebasing merely because master advanced. When an API changes, update its test doubles and build the affected harnesses.

Docs that change in step: `docs/development/verification.md` (drop "Predictive pull-request checks", "Model, history and refresh", "Selection evidence and completion gate", the `ci:run`/`ci:windows` paragraphs; replace the evidence template with a description-only template; add "Fast gate"), `docs/development/testing/ci.md`, `build-options.md`, `simulation-verification.md`, the six benchmark guides, `docs/multiplayer/turn-engine.md:246-254`, `turn-verification.md:153`, `test/README.md`.

### **W3-C One command for revision-keyed fixtures (1 d)**

`tools/refresh_sim_fixtures.py`: (1) build `engine-tests map-generator-golden-test` via `tools/dev_build.py` (dev tier is valid: same-platform checksums must match perf/release); (2) `run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*'` then `--update-fixtures --tag golden` (`GLOB2_TEST_UPDATE_FIXTURES=1`, 16 `[golden]` fixtures via `glob2test::expectGolden`); (3) `map-generator-golden-test <profile> --update` for the host, print other platforms' rows as pending; (4) `test/check_telemetry_simulation.py --update-fixtures`, `test/maxima/check_save_continuation_fixture.py --update-fixtures` (serial); (5) `test/check_sim_revision.py --base origin/master`, non-zero if the record still names the old version; (6) `--from-run <id>`: `gh run download` of `golden-rows-<platform>` artifacts uploaded by the master Windows/macOS generator jobs (`ci-linux-build.yml:693`, `ci-macos.yml:113-125` already run them; add the upload) and merge rows. Update `check_sim_revision.py:33` FIX text and docs. Motivation: 66 bumps, golden changed 50×, 26 refresh-only PRs; co-change over the last 40 bumps: `FourSquares1.g2mr` 40, `verify-trace.txt` 21, `unit-catalog/ablation-checksums.txt` 7, `EngineFixtures.cpp` 6\.

### **W3-D Fast CI gate (D1 1 d observe; D2 0.5 d \+ ten runs)**

**D1 `.github/workflows/ci-fast.yml`** on `pull_request` and `push: master` (keeps the cache warm), concurrency per PR with cancel-in-progress:

| Job | Steps | Expected |
| :---- | :---- | :---- |
| `contracts` | the non-selection steps of `build.yml:41-137` (build-system contracts, `test_run_tests`, `check_sim_revision --base`, `check_entity_random`, `check_header_layering`, docs links, vendor verify) | 3–4 min |
| `linux-fast` | apt deps as `ci-linux-build.yml:87-125`; `.github/actions/sdl3` cache; restore ccache key `ccache-fast-ubuntu-24.04-gcc13-<merge-base sha>` with prefix fallback; `python3 tools/dev_build.py -j4 engine-tests unit-tests src/glob2`; `run_tests.py --tier headless --binary all -j4 --timing-profile test/ci-timings/ubuntu-24.04.json --junit`; save ccache on master pushes only | build ≤5 min warm (the measured 296 s was release \-O3), tests 887 s/4 vCPU ≈ 4 min; total 9–11 min |
| `master-health` | `gh run list --workflow build.yml --branch master --limit 1`; fail when conclusion is failure and `updated_at` \> 24 h ago unless the PR has `ci:repair` | seconds |
| `browser-smoke` (conditional) | only when the diff touches `platforms/web/**` without C++: `cli.spec.js` \+ `build-artifact.spec.js` against the merge-base master `web-client` artifact | \~3 min; a C++-affecting wasm smoke cannot fit 15 min and stays async |

Run D1 non-required for ten PRs; compare with `ci_run_metrics.py` as `verification.md:33-40` prescribes.

**D2 wiring and retirement.** Add a ruleset on `master` requiring `contracts`, `linux-fast`, `master-health` (no "up to date" requirement). `build.yml`: remove the `pull_request` trigger (keep `push: master`, `schedule`, `workflow_call`, `workflow_dispatch`); `ci:full` label triggers it via a small label-dispatch workflow. Delete `ci-model.yml`, `.github/ci/{model,history}.json.gz`, `model-manifest.json`, `ci_predictive.py`, `ci_train_model.py`, `requirements-ci-model.txt`, `test_ci_train_model.py`, `test_ci_predictive.py`; strip `_prediction_inputs`, `CI_PR_CHECK_BUDGET`, `CI_PR_FULL_AUDIT_PERCENT`, `CI_PREDICTIVE_ENABLED` and the `ci:run`/`ci:windows`/`ci:android`/`ci:browsers` labels from `ci_changed_paths.py` and `ci_policy.py` (keep path-based cheap-vs-engine classification for master/nightly; keep `ci_history.py` only if `ci_batch_metrics.py` imports it). `cancel-superseded.yml` stays for PR runs. Nightly unchanged. Investigate the CI ccache regression (Linux build 13 → 41–45 min) as part of W1-1/W1-2 — the CI cache likely suffers the same path/limit problems.

### **W3-E Test tiers, timings, provenance (1 d)**

**E1 provenance (own PR, also W1-4).** `test/build_provenance.py:38-43`: drop the `ls-files --others` loop (no untracked hashing); keep tracked \+ dirty-tracked identity; add `binary_sha256`. `scons/test_provenance.py`: replace the header `Command` with `AddPostAction` on each test `Program` writing `<binary>.provenance.json` (computed inside the action, so no cost at SConscript read); callers `test/SConscript:40-41`, `scons/mobile_build.py:98-99`, `scons/web_build.py:181`. `test/support/TestMain.cpp:15,105`: remove the include; read the sidecar beside the binary, verify the hash, print the same `GLOB2_TEST_PROVENANCE` line (consumers `check_javascript_corpus.py:71-73`, `check_javascript_evidence.py` unchanged) and copy it to `GLOB2_TEST_ARTIFACTS_ROOT`. Verify: edit a comment, rebuild, no `TestMain.o` recompile and no relink.

**E2 tiers and timings.** Tag `src/render/HighResolutionIntegrationHarness.cpp` OpenGL case `[slow]`. `test/run_tests.py:164-190, 644`: add `--tier {headless,display,full}` (`headless` \= no `[display]`, no `[slow]`; default), `--quick` stays an alias for "exclude `[slow]`"; callers pass `--tier full`: `ci-linux-build.yml:474,481`, `ci-macos.yml:103-104,121`, `test/run_coverage.py:204`, `test/ci_native_shard_plan.py:26`. Default `--build-dir` prefers the dev\_fast directory. Populate `test/ci-timings/ubuntu-24.04.json` with `test/build_ci_timing_profile.py` (`minimum=1`, `"provisional": true`) from the last green run, then let master refill with `minimum=10`. Gain: headless 887 s vs 3,658 s (−76%); balanced shards cap \~915 s instead of 1,728 s.

### **W3-F Coverage-based test selection (3 d; Phase 3\)**

Recording (master `native-coverage`, `build.yml:185-231`): `run_tests.py --profile-per-job DIR` sets `LLVM_PROFILE_FILE=DIR/<job-hash>.profraw` per job; after each job `llvm-profdata show --all-functions --counts --text` → nonzero functions (delete the profraw); once per run `llvm-cov export` gives function→file; emit `coverage-map-<sha>.json.gz` (case → source files) as an artifact, 30-day retention. Selection (`test/select_affected.py`, used by `--affected` and `linux-fast`): inputs \= PR diff vs merge-base \+ latest map whose SHA is an ancestor ≤7 days old; run a case when its file set intersects the diff, when it has no record, or when it is in the always-run list (`TurnEngineHarness/*`, `[save-format]`, replay, `check_sim_revision`, `UnitCustomization` ablation, generator goldens); fall back to the full headless tier when no fresh map, or the diff touches headers (v1; v2 may consume the include graph), `SConstruct`, `scons/`, `src/SConscript`, `test/`, `content/`, `.github/`, or any non-C++ file in `src/`. Print the selected inventory in the job summary. Risk: indirect state (globals) — mitigated by the always-run list and the master full run. Replaces the predictor's role at case granularity with an observed, deterministic signal.

### **W3-G Perf workflow (1 d, after W3-A and W1-7)**

`tools/perf_ab.py --bench <name> --baseline <dir|master> --candidate build/<platform>/client/perf[/o3] --pairs N`: resolves `master` to the prebuilt perf baseline for the merge-base SHA; alternating pairs; wraps existing tools by their `--baseline/--candidate` arguments (`tools/memory_benchmark.py:90-91`, `tools/software_render_benchmark.py:16-17`, `tools/release/benchmark_profiles.py:220-222`; `tools/gradient_benchmark.py:315-317` gains `--baseline-binary`); uses `test/run_with_benchmark_cpuset.py` / `run_with_benchmark_governor.py` on Linux at confirm tier. Tiers: screen \= O2, 3 pairs; confirm \= O3, 7 pairs, pinned. Emits the PR Markdown table:

```
| bench | metric | base (sha) | cand (sha) | Δ% | pairs | tier | host |
| gradient/terrain-hazard | ms/tick p50 | 12.4 | 10.9 | -12.1 | 7 | perf-confirm O3 | linux x86_64 |
```

---

## **9\. Master sequence**

| Phase | Weeks (approx.) | Steps | Exit criterion |
| :---- | :---- | :---- | :---- |
| **0 Unblock** | 0.5 | W3-0 H2 OpenSSL fix; W1-9 worktree/disk commands \+ free-space guard (reclaim the \~38 GiB of merged worktrees first) | master builds on the Mac; ≥60 GiB free; `dev_environment.py disk` works |
| **1 Cheap, big wins** (parallel PRs) | 1–2 | W1-1, W1-2, W1-3; W3-E1 provenance; W2-A1 versions \+ W2-G1 guard; W2-A2 Color/SDL; W2-A3 json/AI-id; W3-E2 tiers/timings; W3-A `perf=1`; W3-C regen command; W3-D1 fast gate (observing) | second worktree builds in ≈1 min; no-op after commit no longer relinks tests; −24% recompiles on replay; headless tier is the local default; ten observed fast-gate runs ≤15 min |
| **2 Sharing and policy** | 2–3 | W1-5 token pool; W1-6 shared assets; W1-7 prebuilt baseline; W2-A4, A5; W2-B1..B5; W3-D2 gate required \+ predictor retired; W3-B AGENTS.md \+ docs; W3-G perf\_ab; W3-0 H1 clang check | 4 lanes ≈ 1-lane CPU efficiency; new worktree ≈40 s; perf PRs need no second worktree; ruleset active; −28% recompiles; `Game.h` no longer includes render |
| **3 Profiles and selection** | 2 | W1-8 `compile_profile=core`; W1-9 remaining disk items incl. git ref expiry \+ repack; W1-10 PCH; W1-11 SCons tuning; W3-F coverage selection | Game.h edit in core profile ≈ −30–38% TUs; git store ≈10 GiB; `--affected` works locally |
| **4 Deep decoupling** | 6–10 | W2-C1 MapRouting; W2-C2 SimContext; W2-C3 GenerationWorld; W2-C4a/b weld edges; W2-C5 ratchet | SCC shrinks from 230 objects to the sim core only; generators rebuild only on GenerationWorld.h |
| **5 Module libraries and tests** | 2 | W2-C6 libglob2\_map/sim \+ per-module test binaries | a map or sim test links without GameGUI/GlobalContainer; case-count contract holds |
| **6 Build tool migration** | 3–6 | W1-12 CMake \+ Ninja staged | object-for-object parity; no-op \<1 s; SCons removed |

Total: roughly 4–6 months of part-time maintainer \+ agent work, with the bulk of the benefit (phases 0–3) in the first \~6 weeks.

---

## **10\. Verification and success metrics**

Re-measure these at each phase exit (same commands as the analysis; quiet machine for wall-clock):

| Metric | Baseline (Nov 2026\) | Target after phase 3 | After phase 5 |
| :---- | :---- | :---- | :---- |
| Median TU → repo headers reached | 93 | ≤50 | ≤35 |
| Headers reaching ≥50% of client TUs | 105 | ≤60 | ≤30 |
| Fan-in: Version.h / SDLGraphicContext.h / Scene.h / json.hpp / Game.h / Map.h | 499 / 549 / 371 / 472 / 365 / 403 | 54 / 228 / 93 / 143 / ≤330 / ≤380 | Game.h ≤250, Map.h ≤300 |
| Share of commits rebuilding \>50% of TUs (90-day replay) | 19.7–28% | ≤13% | ≤8% |
| Second worktree, same commit, warm cache (CPU-s compile) | 834 | ≤60 | — |
| Fresh worktree configure \+ asset export | \~260 s | ≤20 s | — |
| 4 concurrent lanes: CPU-s per identical job vs 1 lane | \+40% | ≤+10% | — |
| ccache cleanups per week / hit rate | thousands / 29% | \~0 / \>70% | — |
| Relink of engine-tests after a commit with no code change | yes | no | — |
| Local default test tier (serial) | 3,658 s | 887 s | per-module seconds |
| Required CI check wall time | none (full 187 min advisory) | ≤15 min | ≤15 min |
| Completed master runs green (30-day) | \~0% | ≥80% | ≥90% |
| Perf PR first-commit→merge median | 1.9 h (3.3 h w/ release) | ≤1 h | — |
| Refresh-only fixture PRs | 26 / 2 months | 0 | 0 |
| glob2 disk footprint / merged worktrees retained | \~190 GiB / 10 | ≤90 GiB / 0 | — |
| SCC size (objects) | 230 | ≤230 (unchanged until phase 4\) | sim core only; map tests link without GUI |
| SIM\_REVISION moved by this plan | — | 0 | 0 |

Simulation neutrality on every W2 PR: golden `match verify` byte-identical; per-tick checksums identical macOS arm64 \+ Linux x86-64; save round-trips; hot-accessor call count \= 0 in release per-cell kernels.

---

## **11\. Risks and mitigations**

- **Hidden hot-path regressions** from header moves → every W2 PR runs the call-instruction grep on release objects and `gradient_benchmark`; cached-pointer pattern instead of `-inl.h`.  
- **Profile builds hiding breakage** → CI always full; `dev_build.py check-full`; profile marker refuses verifier/rated roles; AGENTS.md requires full-profile verification of the final revision.  
- **Static archives dropping doctest self-registration** → test TUs stay loose objects; `-force_load`/`--whole-archive` otherwise; case-count contract.  
- **Static-init order shifts and stub ODR** when link order changes → one stub set per executable; headless runs in CI on each W2-C PR.  
- **Coverage-based selection misses** → always-run contracts, header-change fallback, full master run.  
- **Fast gate becomes slow again** → ccache keyed on merge-base, measured on ten runs before it is required; the 15-min budget is a tracked metric.  
- **Destructive disk operations** (worktree removal, git ref expiry) → confirmation prompts; never touch locked/dirty trees; ref expiry only after baselines exist and the maintainer confirms.  
- **CMake/SCons divergence during migration** → one exported source list; CI object-checksum parity; mobile/web last.  
- **Windows/mobile/web build breaks** from include cleanup → `ci:full` on W2-A2 and any PR touching gag headers.

## **12\. Out of scope (explicitly)**

JS port of Maxima or native generators; per-module dylibs; lld; `#ifdef`\-based removal of recording/renderers/online/GPU gradients; string-keyed terrain ids (enum additions still rebuild their includers); distributed cross-OS compilation; changing `SIM_REVISION` for anything in this plan.

## **13\. Where this plan and its evidence live**

- This document: `~/.claude/plans/all-right-i-want-polymorphic-kitten.md`. When implementation starts, copy it to `docs/.work/dev-experience-plan.md` (ignored directory) so it travels with the checkout; it is not meant to be committed.  
- Analysis reports, raw CSVs, scripts and the three workstream designs were written under the session scratchpad `/private/tmp/claude-501/-Users-bradley-glob2/91c4dabb-64c0-4553-86d4-44903fdb686d/scratchpad/analysis/` (`include-graph/`, `velocity/`, `build-system/`, `features/`, `policy-ci/`, `scripting/`, `core-arch/`, `tests/`, `disk/`, `design/`). That location is temporary; copy anything worth keeping to `docs/.work/` or `artifacts/` before the session's scratchpad is cleaned. Every number this plan relies on is reproduced in section 3 so the plan stands without them.  
- Re-measurement scripts worth preserving: `velocity/walk.py` \+ `analyze.py` \+ `counterfactual.py` (per-commit blast replay), `core-arch/scripts/incl.py` \+ `replay_commits.py` (fan-in \+ what-if), `tests/linkgraph.py` (nm-based SCC), `build-system/lane.sh` (lane contention), `scripting/bench/` (QuickJS vs C++ kernels incl. the clang `-O2` repro).

## **14\. Appendix — key file index**

- Policy: `AGENTS.md` (symlinked `CLAUDE.md`), `docs/development/verification.md`, `build-options.md`, `simulation-verification.md`, `development-storage.md`, `docs/multiplayer/turn-engine.md`.  
- Build: `SConstruct` (:40-49, 98-99, 359-364, 515-521), `src/SConscript` (:53), `scons/{dev_build,dev_compile,dev_store,ccache,runtime_assets,renderer_shaders,client_features,sources,build_layout,size_optimization,test_provenance}.py`, `tools/dev_build.py`, `tools/dev_environment.py`, `tools/benchmark_build.py`.  
- Tests/CI: `test/run_tests.py`, `test/tests.py`, `test/build_provenance.py`, `test/support/TestMain.cpp`, `test/support/EngineFixtures.h`, `test/check_sim_revision.py`, `test/ci-timings/`, `.github/workflows/{build,ci-linux-build,ci-macos,ci-model,cancel-superseded}.yml`, `.github/scripts/ci_{changed_paths,policy}.py`.  
- Architecture: `src/map/Map.h` (:118 ComputeExecutor; :941-1266 routing; :1367-1421 gradients), `src/map/MapStateView.h`, `src/game/Game.h` (:33 render include; :297 snapshots; :400-534 draw statics; :596-624 ViewState), `src/team/Team.h` (:157-158 back-pointers), `src/team/stats/TeamStat.h:434`, `src/building/Building.h` (:64 BuildingRoute; :263/:340 material accessors; :591 owner), `src/online/SimVersion.h:83`, `src/game/SimRevision.h`, `src/app/Version.h`, `src/app/FileFormatVersions.h`, `libs/gag/include/{GraphicContext,SDLGraphicContext,GAGSys}.h`, `src/team/BaseTeam.h`, `src/game/GameEvent.h`, `src/game/player/BasePlayer.h`, `src/render/scene/{Scene,WorldSnapshot}.h`, `src/map/MapRenderState.h`, `src/map/types/{TerrainType,TerrainTypeTable}.h`, `src/ai/AI.cpp`, `src/replay/Replay{Writer,Reader}.cpp`, `src/app/GlobalContainer.h`, `src/hud/GameGUI.h`, `src/engine/Engine.h`.  
- Known-good fix: `4b38a8422c` (OpenSSL 4, `LanIdentity.cpp:102`).

