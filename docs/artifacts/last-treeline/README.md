# The Last Treeline: review and evidence

See [design and controls](../../map-generators/LAST_TREELINE.md), the
[initial terrain](terrain-seed71.png), [eight-colony preview](eight-colonies.png),
and [45,000-tick final map](late-game.png).

## Review and revisions

Seven agent review rounds covered source, rendered terrain, game outcomes and tests.
The initial circular islands became elongated wooded banks with sloughs opening into
a dry lakebed. Further revisions added varied bank shapes and noisy edges, near-home
wheat, reserved outpost courts, and checks for productive frontage and routes after
all crop plots fill. Later parameter failures led to a minimum irrigated bank core,
opening seeds near both competing homes, home-ground reservations and bounded shoreline
bends. Validators were preserved, not relaxed to hide those failures.

The reviewer found no remaining implementation or visual blocker. This was an agent
review for the same author, not independent maintainer approval. Translation review
edited all 33 locales, checked unique/nonempty UTF-8 keys, and used forest-belt rather
than alpine-treeline wording. Basque and Persian remain candidates for native polish.

## Parameter study

[Full response tables](control-report.md) and [all final requests/results](parameter-study.jsonl.gz)
are attached. Resource controls cover every 25-point step from 0 to 300; woodland
depth covers 12, 14 and 16. The study used these supported side pairs: 256×256,
256×512, 512×256 and 512×512, with 2–8 colonies.

| Study | Supported successes | Supported failures | Expected unsupported refusals |
|---|---:|---:|---:|
| Individual controls, eight seeds, extra shape extremes | 582 | 0 | 39 |
| Random combinations, seeds 100000–103199 | 2003 | 0 | 1197 |
| Combined endpoints and resource corners | 432 | 0 | 0 |

The corner pass crosses every supported shape/count with workers 1 and 8,
all resource amounts 0 or 300, and every depth; it also tests all 32 resource
endpoint combinations at every depth. The contract harness samples workers 1–8
across shape/count cases; it does not cross every worker count with every other value.
Unsupported requests in the generic study include 128-tile sides. A disk-full
interruption was resumed from retained rows; it is not classified as a map failure.

The first sweep exposed 61 supported failures. Their original requests are retained
in [retained-failures.jsonl.gz](retained-failures.jsonl.gz); all passed after correction.
No failed request was replaced by a different seed. Finite sample coverage is not a
proof over every possible seed.

Every resource slider increased its intended deposit count at every sampled step,
without changing terrain. On four-colony default maps, mean wood increased from
352 tiles at 0% (192 home + 160 neutral) to 1,370 at 300%; wheat rose from 256 to
640. Depth increased mean plot growth potential from 35.8 through 38.3 to 40.9
full-fertility-equivalent tiles. The full tables show the remaining controls.

## Gameplay

[Per-colony outcomes](game-summary.csv) include calibration, a same-seed Hedgerow
Country reference, duel and alliance games, earlier Nicowar rotations, and four
final mixed-AI rotations. Names beginning `final-play-` are the final geometry:
map seed 71, game seed 31, 45,000 ticks, Nicowar/Cortex/Cabino/Maxima, one rotation
per physical assignment. CSV `births` counts worker births; `starved` and `combat`
sum worker and warrior deaths, while `peak` is sampled total population. Every one of those 16 colonies harvested more than its
48 home wood tiles (88–437), demonstrating use of the contested supply. All grew
beyond the starting workers; deaths and fighting occurred rather than four idle economies.

No physical start was consistently doomed across AIs. The southern start performed
better on this seed (mean wheat harvest 1,574 versus 1,127 at the northwest start).
The northwest Cortex colony failed earlier, while other AIs developed there. This is
limited evidence of playability, not a claim of competitive balance. An earlier mixed
alliance game had substantial starvation and a Cabino construction stall; it is retained,
not omitted from the CSV. Final FFA rotations did not reproduce that universal stall.
Human play and cross-platform execution were not performed in this task.

[Initial map](seed71.map.gz) and [late saved game](late-game.game.gz) are attached;
decompress before loading them in the game. Late growth remains contained by visible
sand, with dry starter copses exhausted and wooded banks still present. Bulk logs and
all additional saves remain in `artifacts/last-treeline/` in the task checkout.

## Regression checks

The final [full defaults suite](defaults-final.txt), [focused contracts](contracts-final3.txt)
and [custom setup/replay harness](custom-setup.txt) passed. Focused checks include
finished-terrain irrigation of finite wood, removing renewable grove trees, rejecting
mixed crop plots, and validation after 2,048 unattended growth calls. Golden updates
add only the new generator’s eight macOS rows; existing generator rows remain unchanged.

## Performance and reproduction

Sampling identified full-map shoreline trigonometry as avoidable work. A conservative
radial bound now skips it outside the basin and evaluates country noise only where
needed. Twenty paired 512×512, eight-colony maps (seeds 71–90) had identical saved bytes
before and after. [Paired measurements](profile-paired.json) alternate executable order:
mean CPU for generation plus process startup and saving fell from 0.316 to 0.304 seconds
(about 4%); mean wall time was 0.635 versus 0.634 seconds under shared-machine load.
The in-process final generator averaged about 0.100 CPU seconds with telemetry either
on or off, compared with 0.182–0.209 for Orchard Commons in that run. Do not interpret
busy-machine wall-time differences as a generation-only speedup.

Commands, run from the repository root after building:

```sh
build/src/MapGeneratorDefaultsTest glob2-treeline-contracts --treeline-only
build/src/MapGeneratorDefaultsTest glob2-treeline-profile --treeline-profile
python3 .agents/skills/glob2-map-design/scripts/control_study.py last-treeline ablation --out STUDY --seeds 8
python3 .agents/skills/glob2-map-design/scripts/control_study.py last-treeline random --out STUDY --count 3200
build/src/glob2 --generate-map --generator 70 --map-seed 71 --param width=8 --param height=8 --param teams=4 --rotations 4 --write-map true --report terrain --output-dir FRESH_DIRECTORY
build/src/glob2 --run-game --map-file MAP --game-seed 31 --player nicowar --player cortex --player cabino --player maxima --ticks 45000 --telemetry team-timeline --save final --output-dir FRESH_GAME_DIRECTORY
```

Use a frozen binary for long batches. All results here are macOS arm64; the new
optional generator does not change simulation, save formats or existing generators.

## Recovery onto current master

The original local commit was recovered on 2026-09-20. Its numeric generator ID
changed from 61 to 70 because Marchland now occupies 61. Historical metadata and
measurement records retain their original IDs; current reproduction commands use 70.
