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
| Simulation and orders | `src/Game_sync.cpp`, `src/EngineRun.cpp`, `src/Order*.cpp`; simulation thread and client channels in `src/sim/` |
| Units, buildings and teams | `src/unit/`, `src/building/`, `src/team/` |
| Map state and pathfinding | `src/map/`, especially `gradient/` and `pathfind/` |
| AI implementations | `src/ai/`, behind `AIImplementation` |
| Rendering, menus and editor | `src/render/`, `src/gui/`, `src/*Screen*`, `src/map/edit/`; drawing reads only the extracted Scene in `src/scene/`, see [Scene renderer](docs/development/reference.md#scene-renderer); menu/dialog framework in `libgag/include/ui/` and `src/ui/`, see [UI framework](docs/development/ui-framework.md) |
| Network and multiplayer service | `src/net/`, `src/yog/` |
| Graphics/UI and scripting libraries | `libgag/`, `libusl/`, `src/sgsl/` |
| Builds and platform coverage | `SConstruct`, `src/SConscript`, `scons/`, `.github/workflows/build.yml`, `vcpkg.json` |
| Documentation index | [docs/README.md](docs/README.md) |
| Tests and replay usage | [test/README.md](test/README.md), [docs/development/headless-replays.md](docs/development/headless-replays.md) |
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
  before: `MINIMUM_VERSION_MINOR` in `src/Version.h` is the floor of save formats the
  loader must still read, and it moves far more rarely than `VERSION_MINOR` itself. A
  save-format change needs version-gated loading that keeps existing save files
  working, not just a version bump; check save/load continuity whenever simulation
  state, caches or scheduling change, since a derived cache is not always safe to
  discard on load. Dropping support for existing saves is a compatibility break
  that needs an explicit, called-out decision, never a side effect of an unrelated
  change.
- Replay and network compatibility are a separate question from saves, and reset far
  more readily: a simulation change can invalidate replays and mixed-client games
  without changing a single saved byte. Check `src/ReplayReader.h`, `src/Version.h`
  and `src/yog/` for replay acceptance and network/YOG version gates, and test the
  affected acceptance boundaries directly.

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

Use focused checks while editing, then run the checks selected for the final PR
revision. Keep every affected platform and compatibility boundary covered; changing
shared headers, build inputs or test infrastructure can require broader checks than
the apparent feature area. When an API changes, update its test doubles and build the
affected harnesses. Fixtures that change process-wide SDL drivers or other global
state must restore it or run in an isolated process through the engine test registry.

Fetch current master before final validation and resolve actual conflicts. Do not
merge or rebase merely because unrelated commits advanced master: PR CI tests the
merge result against its recorded base. If newer base changes affect the same
components, dependencies or CI configuration, refresh integration validation.
Respect branch protection and address failures from the selected checks before
merging; do not shorten feedback loops by hiding failures or dropping relevant tests.

For CI speed changes, compare runner queue time with execution time, preserve the
selected-case inventory, and record the before/after job count and validation limits.
Prefer reusing built artifacts and avoiding irrelevant jobs over repeatedly raising
concurrency or rebuilding the same inputs. Keep repair PRs focused so unrelated
feature work does not hold up a validated fix.
