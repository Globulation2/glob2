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
| Map state and pathfinding | `src/map/`, especially `gradient/` and `pathfind/`; generators in `generator/`, the editor in `editor/` |
| AI implementations | `src/ai/`, one directory per AI, behind `AIImplementation` |
| Rendering, menus and editor | `src/render/`, in-game HUD in `src/hud/`, menus and settings in `src/ui/`, each domain's screens in its `screens/` directory, `src/map/editor/`; drawing reads only the extracted Scene in `src/render/scene/`, see [Scene renderer](docs/development/reference.md#scene-renderer); menu/dialog framework in `libgag/include/ui/` and `src/ui/`, see [UI framework](docs/development/ui-framework.md) |
| Network and multiplayer client | `src/net/` (turn netcode in `src/net/turn/`, LAN in `src/net/lan/`), online client in `src/online/` ([client](docs/multiplayer/client.md)), match relay in `src/relay/` ([relay](docs/multiplayer/relay.md)) |
| Online platform (TypeScript: accounts, rooms, matches, JSON contracts) | `platform/`, [platform architecture](docs/multiplayer/architecture.md) |
| Soundtrack sets and the music pipeline | `data/zik/` (one directory per set), `tools/music/` (build and QA), [music pipeline](docs/assets/music-pipeline.md), [style guide](docs/assets/music-style-guide.md); playback in `src/audio/SoundMixer.cpp` |
| Graphics/UI and scripting libraries | `libgag/`, `libusl/`, `src/scripting/` (`javascript/`, `sgsl/`, map scripts in `map/`) |
| Builds and platform coverage | `SConstruct`, `src/SConscript`, `scons/`, `.github/workflows/build.yml`, `vcpkg.json` |
| Documentation index | [docs/README.md](docs/README.md) |
| Application shell and shared utilities | `src/app/` (entry point, settings, command-line modes in `cli/`), `src/audio/`, `src/common/` |
| Tests and replay usage | A domain's tests sit beside its code (`*Test.cpp`, `*Harness.cpp`); cross-domain tests, fixtures and the registry are in `test/`: [test/README.md](test/README.md), [docs/development/headless-replays.md](docs/development/headless-replays.md) |
| Build pitfalls, verification and conventions | [Development reference](docs/development/reference.md) |
| Architecture background | [Historical source-code overview](docs/development/legacy-architecture.txt) |

Use these pointers to find the current implementation, rather than treating this
guide as a substitute for reading it. Update affected documentation in the same PR
when changing described behavior, commands, paths or policies.

## Documentation and temporary evidence

Curate committed documentation deliberately. A file being written in the working
tree, useful during development, or referenced in a pull request is not by itself
a reason to commit it. Add a document only when it has a clear long-term audience,
belongs in a category listed in `docs/README.md`, and will be maintained with the
behavior it describes. Prefer updating an existing guide over adding a new report.

Do not commit development artifacts, validation dumps, dated benchmark reports,
working notes or PR narratives. Use these ignored local directories instead:

- `artifacts/` for generated screenshots, maps, saves, replays, logs, datasets,
  profiles, archives and other review evidence;
- `docs/.work/` for temporary Markdown notes, validation narratives and draft PR
  material.

Before committing a documentation file, remove transient command output, local
paths, dated status reports and redundant screenshots. Attach evidence needed for
review to the pull request or keep it on a dedicated evidence branch. Summarize
durable conclusions in the relevant guide. The legacy
`artifacts/`, `evidence/`, `validation/` and `.work/` directories anywhere under
`docs/` are also ignored to prevent accidental recommits; do not use them for
permanent docs.

## Compatibility reminders

- Preserve engine compatibility and identical simulation execution across supported
  platforms. For changes that can affect simulation results or portability, run the
  same initial state, seed and orders on affected platforms and compare per-tick
  simulation checksums; they must match. See [replay verification](docs/development/headless-replays.md).
  CI builds and matching replay orders alone do not establish equivalent execution;
  report any platform coverage that could not be verified.
- Save-state backward compatibility is deliberately durable and has broken for real
  before: `MINIMUM_VERSION_MINOR` in `src/app/Version.h` is the floor of save formats the
  loader must still read, and it moves far more rarely than `VERSION_MINOR` itself. A
  save-format change needs version-gated loading that keeps existing save files
  working, not just a version bump; check save/load continuity whenever simulation
  state, caches or scheduling change, since a derived cache is not always safe to
  discard on load. Dropping support for existing saves is a compatibility break
  that needs an explicit, called-out decision, never a side effect of an unrelated
  change.
- Replay and network compatibility are a separate question from saves, and reset far
  more readily: a simulation change can invalidate replays and mixed-client games
  without changing a single saved byte. Check `src/replay/ReplayReader.h`, `src/app/Version.h`
  and `src/online/SimVersion.cpp` for replay acceptance and network/sim version
  gates, and test the affected acceptance boundaries directly.
- Online play groups players, AI ratings and match verifiers by sim version, and only
  `SIM_REVISION` in `src/game/SimRevision.h` ties that version to simulation code. Bump it
  in every change that can alter what the simulation computes from the same setup and
  orders (rules, units, AI, order validation, map loading, random number use), even
  when saves and replays stay compatible, and regenerate the golden match record in the
  same change. CI fails when the committed `--verify-match` trace moves without a new
  sim version; see [Simulation version](docs/multiplayer/turn-protocol.md#simulation-version).

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
Hash-pinned isolated environments (`mobile/play-api-requirements.txt`) and
large, hardware-specific extras (`tools/music/requirements-*.txt`) stay separate.
A development dependency that starts to affect runtime becomes a runtime
dependency and needs approval.

## Preserving feel

Engine-correct changes can still change how the game feels to play: pacing, art
style, animation smoothness, economy pressure, difficulty. Call out such effects
explicitly in the PR description. A maintainer actually playing the result is part
of review, not a formality that automated checks replace.

## Review and merge ownership

The author prepares and revises the PR and may merge their own changes, including
core game behavior and policy changes. Approval from a second maintainer is not
required. Independent review is optional.

The compatibility reminders and expectations for preserving the game's feel above
still apply. Back validation claims with artifacts a reviewer can actually check:
save files, replays and checksums; screenshots or video for visual/UX changes;
before/after performance tables with the seeds and commands behind them. A
described-but-unattached test is not evidence.

Address review feedback when provided, and give a concise account of the intended
result, verification and limits in the PR.

## Validation and CI feedback

Keep unfinished PRs in draft. Draft and ready PRs run cheap contracts by default;
becoming ready does not start expensive CI. Relevant local or VM testing is the
standard merge verification path. Maintainers accept the evidence, including for
their own PRs; hosted CI success is not a merge prerequisite. `ci:run` explicitly
requests hosted affected checks and `ci:full` requests the full development matrix,
even in draft. `ci:windows`, `ci:android` and `ci:browsers` expand coverage when
hosted verification is requested; they do not start it on their own. A successful
cheap-only `Relevant checks passed` result establishes neither engine verification
nor acceptance of local evidence.

Choose focused tests from the change's actual risks and explain both coverage and
omissions; local verification need not reproduce the CI matrix. Record evidence
in a PR comment: tested commit SHA and base revision, OS/architecture/toolchain,
dependencies and build flags, exact commands and results, coverage rationale,
omitted checks and limitations. Attach logs and applicable checksums, saves,
replays or screenshots through links reviewers can access; files left only on a
local machine are insufficient evidence. Use the [evidence template](docs/development/reference.md#local-and-vm-pr-verification).
Refresh evidence when later edits affect tested behavior, dependencies or
integration. Preserve all affected simulation determinism, save/load,
replay/network, platform compatibility and simulation-version requirements;
focused testing does not waive those boundaries.

Hosted affected verification keeps the complete primary GCC 13 suite for ordinary
engine changes. The selector adds compatibility checks for changed boundaries; unknown/shared build inputs fail
closed to full development verification. Add new native compatibility suites to
`test/ci-compatibility.json` and shared boundaries to `.github/scripts/ci_policy.py`.
Intentional repeats across platforms, browser engines and renderers must remain
explicit. Release/store packaging, signing, metadata and upload validation belong
in release workflows, not routine PR or master validation.

Full master CI detects regressions asynchronously after merges. Every retained
master push runs the full development matrix, irrespective of tier-reduction
settings. Active master runs finish, with only the newest pending push retained;
never cancel a running master run to clear a merge burst. Nightly is a fallback:
it skips expensive jobs only when available successful full hosted evidence covers
the exact master SHA under the current coverage policy. Manual and release
verification remain separate. Local PR evidence never becomes a hosted checkpoint.

Use focused checks while editing, then verify the final PR revision with justified
relevant coverage and record the evidence. Keep every affected platform and
compatibility boundary covered; changing shared headers, build inputs or test
infrastructure can require broader checks than the apparent feature area.
When an API changes, update its test doubles and build the affected harnesses. Fixtures that change process-wide SDL drivers or other global
state must restore it or run in an isolated process through the engine test registry.

Fetch current master before final validation and resolve actual conflicts. Do not
merge or rebase merely because unrelated commits advanced master: hosted PR
verification, when requested, tests the merge result against its recorded base. If newer base changes affect the same
components, dependencies or CI configuration, refresh integration validation.
Respect remaining branch protections. Hosted checks may be pending or unavailable
at merge; known failures introduced by the PR still require resolution. Existing
master regressions impose no merge restriction: PRs can continue merging while
maintainers diagnose and repair them asynchronously. Retain failure evidence and
prioritize repair; record verification in repair PRs and use subsequent full master
results to confirm recovery. Master need not become green before other PRs merge.

For CI speed changes, record queue delay, execution time, runner minutes and time
to result separately. Preserve the selected-case inventory and compare ten
successful runs with matching event and coverage before claiming savings. Keep
projections distinct from measurements; require a complete hosted master/nightly pass of the current coverage policy
before enabling reduced compatibility tiers.
Reuse artifacts only when source revision, compiler, flags and dependency inputs
match. Prefer reusing built artifacts and avoiding irrelevant jobs over repeatedly raising
concurrency or rebuilding the same inputs. Keep repair PRs focused so unrelated
feature work does not hold up a validated fix.
