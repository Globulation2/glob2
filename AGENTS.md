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

- Preserve engine compatibility and identical simulation execution across supported
  platforms. For changes that can affect simulation results or portability, run the
  same initial state, seed and orders on affected platforms and compare per-tick
  simulation checksums; they must match. See [replay verification](docs/headless-replays.md).
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

The author prepares and revises the PR. By default, an independent reviewer — a
human maintainer other than the author, or an agent acting for a different
maintainer — approves and merges it; another session for the same author is not
independent review.

- The author may merge their own change directly once it is low-risk or backed by
  strong validation evidence, and either all review feedback on it has been
  addressed or no reviewer raised an objection. When risk is unclear, default to
  independent review.
- Features and substantive engine, architecture, balance or gameplay changes need
  explicit approval from a human maintainer other than the author, however well
  validated: preserving the game's feel and weighing design tradeoffs is a human
  judgment call, not something tests establish.
- Policy changes need human maintainer agreement.
- Back validation claims with artifacts a reviewer can actually check: save files,
  replays and checksums; screenshots or video for visual/UX changes; before/after
  performance tables with the seeds and commands behind them. A described-but-
  unattached test is not evidence, and it is not enough on its own to justify
  merging under the first bullet.

Give reviewers a concise account of the intended result, verification and limits.
