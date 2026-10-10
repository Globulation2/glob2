# Contributing to Globulation 2

Use this path to prepare a change, find its technical documentation, and provide
reviewable evidence. The shared [repository instructions](AGENTS.md) define
compatibility, dependency, verification, and review requirements.

## Set up

1. Follow the [development index](docs/development/README.md) to install your
   platform's prerequisites and build with `python3 tools/dev_build.py`.
2. Read the [architecture overview](docs/architecture/README.md) and use the
   repository map in [AGENTS.md](AGENTS.md) to find the implementation.
3. Find the relevant topic in the [documentation index](docs/README.md).

## Make a focused change

Explain the problem, intended behavior, and tradeoffs. Keep unrelated changes
separate and update the canonical documentation alongside the implementation.
For documentation changes, follow the [maintenance guide](docs/development/documentation.md).

Simulation changes require a `SIM_REVISION` bump and an updated golden match
record. Preserve save loading, replay/network acceptance, and identical execution
across affected platforms. See [architecture](docs/architecture/README.md) and
[testing](test/README.md) before choosing verification.

Runtime dependency additions, replacements, or upgrades need explicit maintainer
approval with licensing, size, and platform coverage. Contributor-only tooling
uses exactly pinned development dependencies; see [AGENTS.md](AGENTS.md#dependencies).

## Verify and submit

Choose checks from the change's actual risks using the [test guide](test/README.md).
Use development builds for iteration and release builds for shipping or performance
claims. Include gameplay/visual evidence when the change affects how the game feels.

Keep unfinished pull requests in draft. Record the tested commit and base revision,
platform/toolchain, dependencies, flags, commands, results, and limitations. Attach
review-accessible logs and applicable saves, replays, checksums, or screenshots.
Local-only files do not constitute attached review evidence.

The author may merge their own changes; a second maintainer's approval is optional.
Hosted verification is requested with `ci:run` or `ci:full`; cheap checks alone do
not establish engine verification. Follow the complete
[verification and merge policy](AGENTS.md#validation-and-ci-feedback).

## Find help

Use [Support](SUPPORT.md) for reporting problems. Engine, architecture, and gameplay
proposals are welcome; include enough context to discuss their consequences.
