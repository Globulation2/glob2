# Verify a simulation change

Simulation execution must agree across supported platforms for the same initial state, seed and orders. The authoritative policy is in [AGENTS.md](../../AGENTS.md#compatibility-reminders); this guide turns it into a verification sequence.

## Establish the behavior

Identify whether the change preserves behavior or intentionally changes gameplay. For a regression, demonstrate that the focused test fails on the old implementation and passes with the fix. Keep scenarios, inputs, build flags and binaries reproducible.

## Compare execution

For a behavior-preserving change, run base and candidate binaries of the same tier (the dev build is enough; [build tiers](build-options.md#build-tiers)) with identical maps/saves, settings, seeds and orders. Compare per-tick state checksums; matching replay orders alone do not prove equivalent execution. Repeat serial and threaded execution and compare affected platforms. [Headless replays](headless-replays.md) describes traces and match-record verification.

For a representation refactor, compare named state captures as well: cached
effective values, inventories, reservations, list ordering, pending work and every
random stream can affect future ticks without appearing in an aggregate checksum.
Document any normalization field by field and retain the original captures.
When save/resume resets diagnostic telemetry or derived readers, compare the same
continuation on the base revision and report those differences separately; do not
describe a checksum match as complete snapshot equality.

For an intentional simulation change, test the new behavior, call out its effects on pacing/economy/difficulty, bump `SIM_REVISION` and regenerate the golden match record. Do not use a version bump to hide accidental build or portability differences.

## Simulation diff

`tools/sim_fingerprint.py <glob2>` records what one build simulates on fixed
scenarios: each saved game in `content/games` run 600 ticks (per tick, the aggregate
checksum and every team's checksum and unit and building records), the
FourSquares1 match record replayed by `match verify` (its per-tick state checksums,
even when the record names another sim version), and every generator's map
fingerprint on the golden request set (`MapGeneratorGoldenTest --print`).
`tools/sim_diff.py base.json.gz candidate.json.gz` reports, per scenario, the first
tick and component that differ; it exits 0 when all match and 1 when output
differs. Fingerprint two local builds of your own the same way, running each from
its checkout (`--checkout`) so that it reads its own `content/data`:

```sh
python3 tools/sim_fingerprint.py <base>/build/linux/client/perf/src/glob2 --checkout <base> --out base.json.gz
python3 tools/sim_fingerprint.py build/linux/client/perf/src/glob2 --out candidate.json.gz
python3 tools/sim_diff.py base.json.gz candidate.json.gz
```

The `sim-diff` workflow does this for every pull request whose change reaches the
simulation path (`touches_simulation` in `.github/scripts/ci_policy.py`): it builds
the merge base and the pull request at the perf tier and reports the verdict beside
the two `SIM_REVISION` values in the job summary. It is in shadow mode: a difference
without a bump is a warning, not a failure, while its verdicts are compared with
the sim version check. Equal output does not prove behaviour unchanged; the
scenarios cover only what they run. A full fingerprint takes about twelve minutes,
mostly the generator set; `--parts games,match` skips the generators.

## Check compatibility separately

- **Saves:** load older supported saves and compare uninterrupted play against save/load continuation, including outstanding work and derived caches. Preserve version-gated loading; a cache can change future decisions.
- **Replays:** test acceptance and rejection at the current [ReplayReader](../../src/replay/ReplayReader.h) boundaries.
- **Network:** assess protocol and sim-version admission through [Version.h](../../src/app/Version.h), [SimRevision.h](../../src/game/SimRevision.h) and [SimVersion.cpp](../../src/online/SimVersion.cpp). Unchanged serialized bytes do not establish mixed-client compatibility.

Use the [simulation test scenarios](testing/simulation.md), [AI continuation checks](testing/ai.md) and [map tests](testing/maps.md) relevant to the change.

## Publish evidence

Use the [PR evidence template](verification.md#local-and-vm-pr-verification). Record the tested revision and base, exact commands, OS/architecture/toolchain, flags, results, retained inputs and accessible artifact links. State omitted platform coverage explicitly. Builds and automated tests complement maintainer gameplay review.
