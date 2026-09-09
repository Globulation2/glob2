# Working on Globulation 2

This guide maps the repository and records shared compatibility and review rules.
Choose tools and workflow to suit the task. `CLAUDE.md` is a Git-tracked relative
symlink to this file; edit `AGENTS.md` only. If symlinks appear as plain text in a
checkout, read `AGENTS.md` directly.

## Repository map

| Area | Start here |
| --- | --- |
| Simulation and orders | `src/Game_sync.cpp`, `src/EngineRun.cpp`, `src/Order*.cpp` |
| Units, buildings and teams | `src/unit/`, `src/building/`, `src/team/` |
| Map state and pathfinding | `src/map/`, especially `gradient/` and `pathfind/` |
| AI implementations | `src/ai/`, behind `AIImplementation` |
| Rendering, menus and editor | `src/render/`, `src/gui/`, `src/*Screen*`, `src/map/edit/` |
| Network and multiplayer service | `src/net/`, `src/yog/` |
| Graphics/UI and scripting libraries | `libgag/`, `libusl/`, `src/sgsl/` |
| Builds and platform coverage | `SConstruct`, `src/SConscript`, `scons/`, `.github/workflows/build.yml`, `vcpkg.json` |
| Tests and replay usage | [test/README.md](test/README.md), [docs/headless-replays.md](docs/headless-replays.md) |

| Build pitfalls, verification and conventions | [Development reference](docs/development-notes.md) |
| Architecture background | [Source-code overview](doc/sourceCodeUnderstanding.txt) |

Use these pointers to find the current implementation, rather than treating this
guide as a substitute for reading it. Update affected documentation in the same PR
when changing described behavior, commands, paths or policies.

## Compatibility reminders

- Multiplayer peers must compute the same simulation from the same initial state
  and orders across supported platforms. Check determinism and save/load continuity
  when changing simulation state, caches or scheduling. CI builds and matching replay
  orders alone do not establish equivalent simulation state.
- Save-format changes must bump `VERSION_MINOR` in `src/Version.h`, with version-gated
  loading for older saves or an explicitly approved compatibility break.
- Simulation changes can invalidate replays and mixed-client games without changing
  saved bytes. Check `src/ReplayReader.h`, `src/Version.h` and `src/yog/` for replay
  acceptance and network/YOG version gates; test affected acceptance boundaries.

## Review and merge ownership

The author prepares and revises the PR; an independent reviewer approves and merges
it. An agent acts for its human contributor: another session for the same author
is not independent review.

- An agent acting for a different maintainer may review and merge focused bug fixes
  and behavior-preserving cleanup with appropriate evidence and passing checks.
- Features and substantive engine, architecture, balance or gameplay changes need
  explicit approval from a human maintainer other than the author.
- Policy changes need human maintainer agreement. Resolve outstanding technical or
  design objections before merging; verify approval and required CI for the current
  reviewed head.

Give reviewers a concise account of the intended result, verification and limits.
