# Gzip-by-default maps and saves: validation evidence

This PR makes new `.map`/`.game` files gzip level 6 by default (raw legacy files
keep loading), migrates the repository's 38 standalone `.map`/`.game` files to
that format, and updates the entry points, tests, tooling and docs that assumed
a raw container. This directory holds the size/CPU measurements and repository
migration record referenced from the PR description.

All measurements in this directory were taken on **macOS/arm64 (Darwin 25.6.0,
Apple clang 21.0.0)** — the only platform available in this session. Linux and
Windows execution were **not verified locally**; see "Platform coverage" below.

## Repository migration

`migration-report.json` records, for each of the 38 git-tracked `.map`/`.game`
files: its raw (pre-migration) size and SHA-256, and the migrated `.gz` file's
size. Every migrated file was verified independently (via the system `gzip`
against each file's original git-blob bytes, not just the migration script's
own check) to inflate back to exactly its original SHA-256 before the raw copy
was removed.

- Total original size: 25,925,898 bytes (25.93 MB)
- Total gzip (level 6) size: 2,323,782 bytes (2.32 MB) — 9.0% of original
- This matches the plan's own in-memory level-6 estimate (25.93 MB → ~2.32 MB).

Files inside existing `.tar.gz` validation archives (`test/fixtures/ai-save-performance/`,
`test/fixtures/maxima-food-ledger/`, `test/fixtures/tournaments/`) were left
untouched, per the migration's scope.

## Map generation cost (`report.json` → `map_generation`)

Validated Coral, seed 7, three runs per size/binary, `/usr/bin/time -l` wall
(`real`) time; baseline = commit `af7073c44` (this PR's parent); new = this
PR's `libgag/src/FileManagerGzip.cpp`-based writer.

| Size | Teams | Baseline bytes | New bytes | Reduction | Baseline real (median) | New real (median) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 128×128 | 2 | 999,172 | 35,808 | 96.4% | 0.03 s | 0.03 s |
| 256×256 | 4 | 3,111,372 | 146,826 | 95.3% | 0.08 s | 0.09 s |
| 512×512 | 8 | 10,679,644 | 495,010 | 95.4% | 0.25 s | 0.27 s |

Command shape (repeated per size/binary):

```sh
<binary> --generate-map coral --seed 7 --width <N> --height <N> --teams <T> --output <path>.map
```

Map generation writes one compressed output; the wall-time cost of compressing
it is within run-to-run noise at these sizes.

## Integrated save cost (`save_cost_report.json`)

Three fixed saved states run 2,048 further ticks with `--save initial --save
final --save every:512` (six snapshots written per run — one of which,
`checkpoint-2048`, coincides with `final`), three runs per fixture/binary:

- `games/gd-small-2ai.game.gz` (repository fixture; inflated to a temporary raw
  file for the baseline binary, which cannot read `.gz`)
- `mixed-initial.game` — the archived mixed-AI initial save from
  `test/fixtures/ai-save-performance/validation-20260914.tar.gz` (`inputs/mixed-initial.game`)
- `maxima-initial.game` — the archived Maxima initial save from the same archive
  (`inputs/maxima-initial.game`)

| Fixture | Baseline real (median) | New real (median) | Baseline user CPU (median) | New user CPU (median) | Baseline final size | New final size | Reduction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| gd-small-2ai | 0.21 s | 0.23 s | 0.17 s | 0.21 s | 1,021,304 B | 69,785 B | 93.2% |
| mixed-ai-initial | 3.52 s | 4.76 s | 2.60 s | 4.70 s | 50,315,153 B | 3,475,008 B | 93.1% |
| maxima-initial | 1.18 s | 1.72 s | 0.81 s | 1.69 s | 20,113,665 B | 1,452,055 B | 92.8% |

Command shape (repeated per fixture/binary, fresh `--output-dir` per run):

```sh
<binary> --run-game --load-game <fixture> --ticks 2048 \
  --save initial --save final --save every:512 --output-dir <dir>
```

**Reading the CPU numbers:** for saves in the tens-of-MB range (mixed-ai-initial,
maxima-initial), gzip compression of six large snapshots meaningfully raises
user CPU time — roughly +80% for mixed-ai-initial, +109% for maxima-initial —
in exchange for ~93% smaller files on disk. For the much smaller gd-small-2ai
fixture the CPU difference is within noise. This is the actual space/CPU
trade-off of the change for snapshot-heavy headless workloads (e.g. `--save
every:N` on a large save); wall-clock time stays close to baseline because
compression parallelizes with I/O poorly on these single-threaded writes but
the absolute times remain small. This measurement, not a preset cutoff, is
what a reviewer should weigh.

## Load-only comparison (`report.json` → `load_only`)

Same content, raw vs. gzip container, new binary only, three runs each,
`--preview-map <file> --json <tmp>`:

| Fixture | Raw bytes | Gzip bytes | Raw real (median) | Gzip real (median) |
| --- | ---: | ---: | ---: | ---: |
| gd-small-2ai | 197,439 | 17,729 | 0.03 s | 0.03 s |
| mixed-ai-initial | 6,742,207 | 256,660 | 0.17 s | 0.14 s |
| maxima-initial | 1,964,441 | 89,702 | 0.07 s | 0.06 s |

Load-only time is dominated by parsing/thumbnail work, not (de)compression;
gzip loads are as fast as or faster than raw loads here, consistent with
reading less off disk.

## Build hashes

- Baseline binary (`af7073c44`, `release=1 server=0`): SHA-256
  `bbdbefc8d5601790f9c8918b7543ca47cb60a5d3a8133c65d566a6fdb441149d`
- New binary (this branch, `release=1 server=0`): SHA-256
  `0ef235dadfd0b26adabab565d1364b22de46d2bb044f856e57c47e8d69f51e4b`

## Test coverage exercised

- `python3 test/run-savegame-safety-tests.py build/src/SavegameSafetyHarness` —
  full pass against migrated fixtures (gzip round trip, atomic write/failure
  paths, deferred-SHA1 backpatch, truncated/corrupt map rejection, autosave).
- `python3 test/test_map_cli.py build/src/glob2` and
  `python3 test/test_map_report.py build/src/glob2 build/src/MapReportHarness` —
  full pass; `--output`/`--preview-map` round trips, the three checked-in legacy
  save previews, and the premade-map glob all updated for `.gz`.
- `python3 test/run_lan_session_test.py build/src/LANSessionHarness` — full
  pass; verified the downloaded `.gz` bytes match the source fixture's `.gz`
  bytes exactly end to end (host's already-compressed local copy sent without
  re-gzipping; new receiver stores the download as `.gz` without unzipping).
- `TeamStatsSaveHarness`, `BuildingFootprintHarness --load`,
  `EnteringUnitSaveHarness --load` — full pass using `test/inflate_gzip_fixture.py`
  to materialize genuinely raw temporary files from the migrated `.gz`
  fixtures, so these still exercise loading uncompressed legacy saves.
- `MaximaFarmingIntegrationTest`, `MaximaEconomyRegressionTest` — full pass
  (via `test/run_maxima_implementation_regressions.py --reuse-built-objects`).
- Manual: `--run-game --generator ... ` combined headless flow (map generation
  feeding directly into `--map-file`) end to end.

## Platform coverage

Everything above ran on **macOS/arm64** only. Per the plan's own instruction to
report unverifiable coverage: Linux and Windows execution of this change were
**not performed** in this session (no such machine was reachable at the time
of this validation pass), so:

- Cross-platform simulation-checksum equivalence for the (unchanged) serialized
  bytes inside the gzip container was not independently re-verified on those
  platforms.
- The CI workflow changes (`.github/workflows/build.yml`) that inflate legacy
  fixtures to temporary raw files before `--legacy`/`--load` were written to be
  portable (plain `gzip` module in Python, no platform-specific tool), but were
  only exercised locally on macOS, not through actual Linux/Windows CI runners.
- MSYS2/Windows path handling for the new `.gz`-aware code (`FileManagerGzip.cpp`,
  `Headless.cpp`) was reviewed but not built or run on Windows.

A maintainer with Linux/Windows access (or a CI run of this branch) should
confirm the workflow changes and the gzip round trip on those platforms before
merging.
