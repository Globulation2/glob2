# Working on Globulation 2

This guide maps the repository and records shared compatibility and review rules.
Choose tools and workflow to suit the task. `CLAUDE.md` is a Git-tracked relative
symlink to this file; edit `AGENTS.md` only. Likewise, agent skills live in
`.agents/skills/`, and `.claude/skills` is a tracked relative symlink to it so Claude
Code discovers them; add or edit skills under `.agents/skills/` only. If symlinks
appear as plain text in a checkout, read `AGENTS.md` and `.agents/skills/` directly.

## Repository map

| Area | Start here |
| --- | --- |
| Simulation and orders | `src/game/` (`Game_sync.cpp`, orders in `orders/`, rules in `rules/`), `src/engine/EngineRun.cpp`; simulation thread and client channels in `src/engine/sim/`; replays in `src/replay/` |
| Units, buildings and teams | `src/unit/`, `src/building/`, `src/team/` (statistics in `team/stats/`), `src/resource/`; each holds its type tables in `types/` and its drawing or HUD code in `render/` or `hud/` |
| Map state and pathfinding | `src/map/`, especially `gradient/` and `pathfind/` (routing API: `gradient/MapRouting.h`, reached as `map->routing()`); generators in `generator/`, the editor in `editor/` |
| AI implementations | `src/ai/`, one directory per AI, behind `AIImplementation` |
| Rendering, menus and editor | `src/render/`, in-game HUD in `src/hud/`, menus and settings in `src/ui/`, each domain's screens in its `screens/` directory, `src/map/editor/`; drawing reads only the extracted Scene in `src/render/scene/`, see [Scene renderer](docs/architecture/rendering.md#scene-renderer); menu/dialog framework in `libs/gag/include/ui/` and `src/ui/`, see [UI framework](docs/development/ui-framework.md) |
| Network and multiplayer client | `src/net/` (turn netcode in `src/net/turn/`, LAN in `src/net/lan/`), online client in `src/online/` ([client](docs/multiplayer/client.md)), match relay in `src/relay/` ([relay](docs/multiplayer/relay.md)) |
| Online platform (TypeScript: accounts, rooms, matches, JSON contracts) | `services/online/`, [platform architecture](docs/multiplayer/architecture.md); hosting and deployment scripts in `deploy/` |
| Game content (the runtime tree) | `content/` holds `data/`, `maps/`, `campaigns/`, `scripts/` and `games/` under the names the game loads them by: the file manager adds a checkout's `content/` as a search root, packages install its directories at their root, and online sim-data hashes use the paths relative to it. Source art and provenance live in `content-src/` |
| Platform shells and packaging | `platforms/` (`web/`, `platforms/mobile/`, `macos/`, `platforms/windows/`, `linux/`, `stores/`); `debian/`, `snap/` and `fastlane/` stay at the root because dpkg, snapcraft and F-Droid expect them there |
| Soundtrack sets and the music pipeline | `content/data/zik/` (one directory per set), `tools/music/` (build and QA), [music pipeline](docs/assets/music-pipeline.md), [style guide](docs/assets/music-style-guide.md); playback in `src/audio/SoundMixer.cpp` |
| Graphics/UI and scripting libraries | `libs/gag/`, `libs/usl/`, `src/scripting/` (`javascript/`, `sgsl/`, map scripts in `map/`) |
| Builds and platform coverage | `SConstruct`, `src/SConscript`, `scons/`, `.github/workflows/build.yml`, `vcpkg.json` |
| Documentation index | [docs/README.md](docs/README.md) |
| Application shell and shared utilities | `src/app/` (entry point, settings, command-line modes in `cli/`), `src/audio/`, `src/common/` |
| Tests and replay usage | A domain's tests sit beside its code (`*Test.cpp`, `*Harness.cpp`); cross-domain tests, fixtures and the registry are in `test/`: [test/README.md](test/README.md), [docs/development/headless-replays.md](docs/development/headless-replays.md) |
| Build pitfalls, verification and conventions | [Development guides](docs/development/README.md) |
| Architecture overview | [Current system overview](docs/architecture/overview.md) |

Use these pointers to find the current implementation, rather than treating this
guide as a substitute for reading it. Update affected documentation in the same PR
when changing described behavior, commands, paths or policies.

## Documentation and evidence

Commit documentation only when it has a long-term audience, a category in
`docs/README.md`, and will be maintained with the behavior it describes. Prefer
updating an existing guide over adding a new report. Never commit development
artifacts: screenshots, dumps, dated benchmark reports, working notes or PR
narratives. Generated files go under the ignored `artifacts/`, notes and draft PR
text under `docs/.work/`; the legacy `artifacts/`, `evidence/`, `validation/` and
`.work/` directories anywhere under `docs/` are ignored too.

Evidence for a pull request lives in its description: the commands run, result
counts, checksums, a short before/after table and at most a few screenshots.
Anything that does not fit there is not kept; there are no evidence branches,
artifact repositories or committed dumps. Summarize durable conclusions in the
relevant guide instead.

Broad AI experiments use the storage controls in `tools.tournaments` (see
[experiment storage](docs/tools/tournaments.md#experiment-storage)); standalone
runners must enforce equivalent per-attempt, campaign and free-space limits.
Default to results, seeds, frozen inputs and compact telemetry, compress logs while
capturing them, and never silently truncate a measurement or count storage
failures as successful games.

## Compatibility reminders

- **Simulation path.** Code under `src/game/`, `src/unit/`, `src/building/`,
  `src/team/`, `src/resource/`, `src/map/` (including `gradient/`, `pathfind/` and
  `generator/`), `src/field/`, `src/ai/`, `src/engine/sim/`, `src/replay/`,
  `src/net/turn/`, `src/scripting/`, `libs/usl/`, `third_party/quickjs-ng/`, the
  simulation data files listed by `deploy/sim_version.py`, and the strict numeric
  flags in `src/SConscript` (`-fno-fast-math -ffp-contract=off`) is the simulation
  path. A change that can alter what it computes needs matching per-tick checksums
  for the same state, seed and orders on Linux and one other platform you can run
  (macOS or a browser build), using the dev build; see
  [simulation verification](docs/development/simulation-verification.md). A change
  to optimization flags or compilers needs a same-platform `-O0` against `-O3`
  checksum match instead. Changes outside the path need no cross-platform run; say
  so in the PR.
- **Sim version.** `SIM_REVISION` in `src/game/SimRevision.h` is the only thing that
  ties online rooms, ratings and verifiers to simulation behavior. Bump it in every
  change that can alter what the simulation computes from the same setup and
  orders (rules, units, AI, order validation, map loading, random number use,
  scripting), even when saves and replays stay compatible, and run
  `python3 tools/refresh_sim_fixtures.py` in the same change: it regenerates every
  revision-keyed fixture, and CI fails when the committed `match verify` trace moves
  without a new sim version ([Simulation version](docs/multiplayer/turn-engine.md#simulation-version)).
- **Saves** are durably compatible: `MINIMUM_VERSION_MINOR` in `src/app/Version.h` is
  the floor of save formats the loader must still read, and it moves far more
  rarely than `VERSION_MINOR`. A save-format change needs version-gated loading that
  keeps existing files working, not just a version bump; check save/load continuity
  whenever simulation state, caches or scheduling change, since a derived cache is
  not always safe to discard on load. Dropping save support is an explicit,
  called-out decision, never a side effect of an unrelated change.
- **Replays and network** compatibility is a separate question from saves and resets
  more readily: a simulation change can invalidate replays and mixed-client games
  without changing a saved byte. Check `src/replay/ReplayReader.h`,
  `src/app/Version.h` and `src/online/SimVersion.cpp` and test the acceptance
  boundaries you move.
- **Headers.** Keep version constants out of headers and simulation headers free of
  rendering, HUD, UI and the full SDL umbrella; `test/check_header_layering.py`
  enforces layering and cycles and reports fan-in budgets
  ([header dependencies](docs/development/conventions.md#header-dependencies)).

## Build tiers

| Tier | Command | Use it for |
| --- | --- | --- |
| dev | `python3 tools/dev_build.py` (`-O0 -g1`, compiler cache, all features) | editing, tests, checksum and golden traces, save/replay checks |
| perf-screen | `python3 tools/dev_build.py perf=1` (`-O2`, full profile, symbols) | the first measurement of a performance change |
| perf-confirm | `python3 tools/dev_build.py perf=1 opt=3` | the number you quote in a performance PR |
| release | `scons release=1` and the platform packaging workflow | shipping and package verification only |

Never ship a dev or perf build, and never quote release measurements as PR
evidence: perf builds exist so that every performance claim is reproducible from
one command. Compare against the prebuilt master baseline
(`tools/dev_baseline.py`, `tools/perf_ab.py --baseline master`) rather than a second
worktree. Plain `scons` gives full debug information; `pch=1` and `unity=1` are
development experiments. `compile_profile=core` leaves most generators, three AIs
and the editor out of focused dev builds ([core profile](docs/development/build-options.md#core-compile-profile));
final PR verification always uses the default full profile. Simulation checksums must match between the dev and perf
builds of one platform; do not bump the sim version to mask a build-induced
mismatch. Fast-build success alone does not establish release readiness.

Every checkout shares one compiler cache, runtime-asset export and CPU token pool,
so parallel lanes cost little more than one; see
[build options](docs/development/build-options.md) for cache and lane details.
`python3 tools/dev_environment.py disk`, `worktrees` and `builds --gc` report and
reclaim space, and builds stop before the disk fills
([development storage](docs/development/development-storage.md#reclaiming-space)).

## Dependencies

Treat runtime and development dependencies differently.

- **Runtime dependencies** are anything linked into, loaded by or shipped with the
  game on any platform, or run by the relay or online platform services in
  production: C/C++ libraries, `vcpkg.json` and pinned SDL/codec prefixes, browser
  bundle packages, platform service packages and assets fetched at run time. Keep
  this set small and prefer native code or libraries already in use. Adding,
  replacing or upgrading one needs explicit maintainer approval called out in the
  PR, with licence, size and platform coverage, and the usual compatibility checks.
- **Development dependencies** serve only contributors and tooling: Python
  scripts, tests, analysis and plotting, asset and music pipelines, CI helpers.
  Add or upgrade them without approval, provided nothing at runtime imports, links
  or ships them, and shipped output does not change unless the PR reviews that
  change as usual. The minimize-dependencies preference is for the runtime core; in
  tooling, prefer a well-known library (numpy, scipy, pandas, scikit-learn,
  matplotlib, Pillow) over hand-written equivalents.

Pin exact versions. General Python tooling installs from `requirements-dev.txt`;
add new packages there, or in a focused requirements file that it includes.
Hash-pinned isolated environments (`platforms/mobile/play-api-requirements.txt`) and
large, hardware-specific extras (`tools/music/requirements-*.txt`) stay separate.
A development dependency that starts to affect runtime becomes a runtime
dependency and needs approval.

## Preserving feel

Engine-correct changes can still change how the game feels to play: pacing, art
style, animation smoothness, economy pressure, difficulty. Call out such effects
explicitly in the PR description. A maintainer actually playing the result is part
of review, not a formality that automated checks replace.

## Evaluating AI changes

Use `tools.tournaments` for standard and custom AI assessments. New tournament
CLI games and experiment planners default to 95% probability-based early victory;
freeze the threshold in the cohort and use an explicit zero only when playing
out complete end games is part of the assessment. Resolve tick-cap survivors by
final win probability rather than the historical economic calculation.

Use the **adjusted win-probability trajectory score** (`log_time_outcome_v1`,
log curvature `c=2`) as the primary continuous effectiveness metric alongside
wins, losses and draws. It integrates the retained probability curve on a log
clock through the fixed game tick limit, then credits a declared win with 1 and
a loss with 0 for the remaining horizon. Probability-based early stops receive
the same outcome credit, explicitly labelled predicted rather than actual.
Tick caps receive no invented terminal tail. Shared winners each receive full
credit; count an alliance once. Use `tools.tournaments.probability.group_score`
or the standard reports, not the legacy linear area/observed-time mean. Compact
results retain a versioned log integral before dropping curves; old compact
records without that integral are unavailable, never silently approximated.
Compare paired seeds and seat rotations, report confidence intervals and missing
coverage, and keep metric version, horizon, model, stopping rules, opponents and
map distributions comparable. Retain actual outcomes and completion times as
checks against improving a model score without improving play. See
[tournament scoring](docs/tools/tournaments.md#whole-game-probability-scores).

## Review and merge

The author prepares, verifies and merges their own PR, including core behavior and
policy changes; independent review is optional. The PR description states the
intended result, the commands run with their results, what was not covered and
why. Visual or feel changes include a screenshot or a sentence on how it plays; a
maintainer playing the result is part of review. Performance changes include the
table from `tools/perf_ab.py`. Address review feedback when provided.

## Validation and CI

Merging waits for the **fast gate** (`.github/workflows/ci-fast.yml`, target under
15 minutes): cheap contracts, the sim-version check, header layering, a Linux build
and the headless test tier. Everything else (full Linux matrix, Windows, macOS,
Android, browsers, coverage) runs on master every six hours and is not a merge
prerequisite. Add the `ci:full` label to run the full matrix on a PR whose
change touches build inputs, shared headers, platform shells or test
infrastructure. Keep unfinished PRs in draft.

**Master red rule.** When the newest decided full master run has had a gating job
failing for more than 24 hours, the next merge must be the repair or a revert,
labelled `ci:repair`. The gate's `master-health` job judges runs only by their
gating jobs (cancelled ones decide nothing) and only warns until master has stayed
green for about two weeks, when its `ENFORCED` flag is turned back on. Keep
repair PRs focused, retain failure evidence and confirm recovery with the next
full master run. Before landing, rebase on current master; never merge a branch
whose rebase stopped on a conflict.

Locally, `python3 test/run_tests.py` runs the headless tier; `--tier full` adds
display and slow cases; `--affected` runs only the cases whose recorded coverage
intersects your change. Choose focused checks from the change's actual risks, keep
every affected compatibility boundary covered, and fetch current master before
final validation without rebasing merely because master advanced. When an API
changes, update its test doubles and build the affected harnesses; fixtures that
change process-wide SDL drivers or other global state must restore it or run in
an isolated process. See [PR verification and CI](docs/development/verification.md).
