# Verify a simulation change

Simulation execution must agree across supported platforms for the same initial state, seed and orders. The authoritative policy is in [AGENTS.md](../../AGENTS.md#compatibility-reminders); this guide turns it into a verification sequence.

## Establish the behavior

Identify whether the change preserves behavior or intentionally changes gameplay. For a regression, demonstrate that the focused test fails on the old implementation and passes with the fix. Keep scenarios, inputs, build flags and binaries reproducible.

## Compare execution

For a behavior-preserving change, run base and candidate optimized binaries with identical maps/saves, settings, seeds and orders. Compare per-tick state checksums; matching replay orders alone do not prove equivalent execution. Repeat serial and threaded execution and compare affected platforms. [Headless replays](headless-replays.md) describes traces and match-record verification.

For an intentional simulation change, test the new behavior, call out its effects on pacing/economy/difficulty, bump `SIM_REVISION` and regenerate the golden match record. Do not use a version bump to hide accidental build or portability differences.

## Check compatibility separately

- **Saves:** load older supported saves and compare uninterrupted play against save/load continuation, including outstanding work and derived caches. Preserve version-gated loading; a cache can change future decisions.
- **Replays:** test acceptance and rejection at the current [ReplayReader](../../src/replay/ReplayReader.h) boundaries.
- **Network:** assess protocol and sim-version admission through [Version.h](../../src/app/Version.h), [SimRevision.h](../../src/game/SimRevision.h) and [SimVersion.cpp](../../src/online/SimVersion.cpp). Unchanged serialized bytes do not establish mixed-client compatibility.

Use the [simulation test scenarios](testing/simulation.md), [AI continuation checks](testing/ai.md) and [map tests](testing/maps.md) relevant to the change.

## Publish evidence

Use the [PR evidence template](verification.md#local-and-vm-pr-verification). Record the tested revision and base, exact commands, OS/architecture/toolchain, flags, results, retained inputs and accessible artifact links. State omitted platform coverage explicitly. Builds and automated tests complement maintainer gameplay review.
