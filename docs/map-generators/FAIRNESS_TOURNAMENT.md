# Map fairness tournament

A generator's start-quality score (`MapGeneration::scoreStarts`) says how evenly a map shares
out wood, wheat, room and distance to rivals. It does not say who wins. The fairness tournament
measures that directly: it plays free-for-all games on generated maps with the same AI in every
slot and asks whether some start positions keep winning, and by how much.

## Running it

Build the optimized client and the study tool first:

```bash
scons release=1 -j3
scons release=1 -j3 map-generator-study
```

Then run a preset:

```bash
python3 tools/map_fairness_tournament.py run smoke      # quick end-to-end check
python3 tools/map_fairness_tournament.py run standard   # the standard measurement
python3 tools/map_fairness_tournament.py run baseline   # engine team-index bias, symmetric maps only
```

The `baseline` preset plays only Symmetric arena, whose colonies are identical by construction. Its
wins by start should come out uniform, and its wins by team index measure the engine's own
processing-order bias with no map effect mixed in.

Results go to `artifacts/map-fairness/<name>/` (ignored by Git). A run resumes where it stopped:
finished maps and games are kept, so re-running the same command after an interruption only plays
what is missing. `--fresh` discards the run's earlier outputs. A changed configuration refuses to
mix into an old directory unless `--fresh`, `--name` or `--out` is given. `summarize <run
directory>` recomputes the report from the stored results.

Presets live in `tools/map-fairness/*.json`. Most keys can also be set on the command line
(`--generators 15,9 --map-seeds 1001-1004 --games-per-rotation 2 --size 128 --colonies 4
--tick-cap 90000 --jobs 3 --verify 2`):

| Key | Meaning |
| --- | --- |
| `generators` | Generator ids; `controls` (`{"<id>": {"control": value}}`) overrides their defaults. |
| `map_seeds` | Map seeds, e.g. `"2001-2012"`. One map per generator and seed. |
| `candidates` | Rolls per map. The lobby keeps the best-scoring of 5 rolls derived from its seed, so the default reproduces the maps players get. `0` uses the seed as is. |
| `size`, `colonies`, `workers` | Map side in tiles (a power of two), colonies, starting workers (generator default when absent). |
| `rotations` | `"all"` plays every cyclic rotation of team numbers over starts (see below). |
| `games_per_rotation` | Engine seeds per rotation. Games per map = rotations x this. |
| `ai` | The AI in every slot (default `nicowar`). |
| `tick_cap` | Ticks after which an unfinished game is stopped and adjudicated. |
| `jobs` | Concurrent processes. Results do not depend on it. |
| `verify_games` | Finished games re-run from scratch at the end; the report says whether they matched. |

Everything is seeded, so the same preset reproduces the same maps, games and report.

`python3 test/test_map_fairness_tournament.py` checks the adjudication, the log parsing and the
statistics against synthetic engine output and known values. It needs no build and plays no games.

## What a run does

1. **Map production.** `MapGeneratorStudy <id> <seed> <profile> w= h= teams= quality candidates=5
   save=<prefix> rotations=N` generates the map through `GenerationService`, choosing the roll
   exactly as `CustomGameScreen::generateMap()` does, and saves it as a playable `.map`. Next to the
   map files, `map.json` records the generator id and revision, the request (size, colonies,
   workers, every control value), the map seed and chosen roll, the start of every colony, the
   start-quality report and the Git revision.
2. **Rotations.** The same file is written once per rotation `r`, with the generator's colony `t`
   relabelled as team `(t + r) mod N`. Terrain and every colony object keep their exact state; only
   the team number each object answers to changes (unit and building ids, per-tile team masks, team
   masks). The tool checks every rotation from its saved bytes: the terrain is unchanged, each
   colony sits under its new team number, every tile reference resolves, and relabelling N times
   reproduces rotation 0 byte for byte. In the games, each team's reported start must match too.
   Rotation 0 is the generated map as read back from its own file, which is what every game
   loads. It differs from a direct save of the freshly generated `Game` only in the header's
   20-byte content SHA1, which the fresh `Game` computes before the real map offset is patched in.
3. **Matches.** Each rotation is played `games_per_rotation` times with
   `glob2 -test-games-nox 1 --map <map> --matchup nicowar,...`, with the engine seed pinned by
   `GLOB2_TEST_SEED` and nothing else varied. Every game runs in its own `GLOB2_USER_DIR`, which
   holds the map, so it never touches `~/.glob2`. Winning conditions are the lobby's free-for-all
   defaults, prestige victory included. `GLOB2_TEAM_RESULTS=1` makes the engine print one
   `GLOB2_TEAM_RESULT` line per team (outcome, elimination tick, start, prestige, units, buildings),
   and `GLOB2_TEST_MAX_TICKS` sets the tick cap. See [headless replays](../headless-replays.md).

Per game, `games.csv` has the winner's start and team, the winning start's coordinates, how the
game ended (`elimination`, `prestige` or `cap`), elimination order with ticks, placements, wall time
and the checksum at the cap. `colonies.csv` has one row per colony per game with its final state and
start-quality factors; `maps.csv` has one row per map.

## Separating the start from the team index

In a headless game the team index is not neutral. Teams step in index order, the AIs issue orders
in player order, and `-test-games-nox` adds a passive local player on team 0, whose AI therefore
polls last. On a single map, start `t` is always team `t`, so a start that wins more often cannot be
told apart from a team index that does.

Rotations break that link. Over the N rotations every team index plays every start exactly once, so:

- **wins by start**, pooled over rotations, carry no team-index advantage, because each start had
  every index equally often; and
- **wins by team index**, pooled over rotations, carry no start advantage.

Both tallies come from the same games. Rotation balancing makes each test conservative when the
other effect is present: it removes that effect's mean and slightly lowers the variance.

Symmetric arena (generator 15) is the baseline. Its colonies have exactly the same ground by
construction, so its wins by start should be uniform, and its wins by team index show the engine's
own team-index bias with no map effect at all.

## What the starts themselves say

Before blaming the maps for a win-rate skew by colony index, ask whether the placement code hands
any index a better start. `tools/colony_start_metrics.py` rolls every playable generator over a
range of seeds with the study tool's `quality` output and averages each colony's measured start by
its index. On 2026-09-12, at 256x256 with 4 colonies over 40 seeds of every playable generator
(680 maps), the pooled means were flat to the last digit that matters:

| Colony | Start score | Wheat distance | Wood distance | Room | Isolation | Nearest rival | Build sites |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 0.673 | 3.9 | 6.6 | 0.811 | 0.996 | 84.3 | 875 |
| 1 | 0.669 | 3.8 | 6.2 | 0.803 | 0.994 | 86.0 | 890 |
| 2 | 0.672 | 4.0 | 6.3 | 0.804 | 0.994 | 82.6 | 891 |
| 3 | 0.669 | 3.8 | 6.3 | 0.798 | 0.994 | 84.5 | 888 |

So the skew a partial tournament run found (colonies 2 and 3 winning about twice as often as 0
and 1, pooled over two of the four rotations) is not in what colonies are given: not in their
resources, their room, or how far their nearest rival stands. What remains is how the games
unfold from equal starts, which only full-rotation tournament games can separate from the team
index. Per generator the table is noisier, and two of the older generators do show an ordering
effect in nearest-rival distance (Islands' later colonies and Rugged archipelago's colony 0 sit
closer to a rival); the script prints the per-generator tables for that.

## What a full-rotation run says

The `smoke` preset played in full on 2026-09-12 (Symmetric arena, Contested commons, Watershed
and Crater lakes; three 128x128 maps each; every rotation twice; 96 games, 82 decided by
elimination) separates the two questions the partial run could not:

- **No engine team-index bias is detectable.** Pooled over all rotations, wins by team index were
  28 / 21 / 23 / 24 (p 0.80); on the Symmetric arena baseline 9 / 2 / 7 / 6 (p 0.21).
- **No colony-index skew replicates.** Pooled per generator, wins by the generator's colony index
  were 8 / 8 / 7 / 1 for Contested commons, 7 / 9 / 4 / 4 for Watershed and 1 / 8 / 6 / 9 for
  Crater lakes: no shared pattern, and nothing like colonies 2 and 3 winning twice as often. The
  partial run's skew came from two rotations of unevenly dominated maps.
- **Individual maps are grossly unfair, and that is the real defect.** On seven of the nine
  asymmetric maps one start won seven or eight of its eight games whichever team played it
  (Contested commons: start 0, start 1 and start 2 on its three maps; Watershed and Crater lakes
  one map each at 8 of 8). Position bias was 29 to 35 percentage points against a 14-point floor
  for a fair map; Symmetric arena, identical starts by construction, sat at 0.
- **The start scorer only partly sees it.** Within-map rank correlation between start score and
  win share was 0.68 for Crater lakes, 0.59 for Contested commons and -0.17 for Watershed; the
  scorer's favourite won 71%, 62% and 17% of those generators' games. Whatever makes a start
  dominate is only partly resources, room and distance.

So the work ahead is per-map, not per-index: find what a dominant start has that the scorer does
not measure (expansion room in the direction of the commons or the delta, who is reachable first,
chokepoints), teach the scorer, and let the lobby's best-of-five reject the lopsided rolls. The
run's `summary.md`, `games.csv` and `colonies.csv` are the material for that.

## Adjudication

A game ends when one colony has defeated every other, when total prestige reaches the lobby
threshold (the highest-prestige colony wins), or at the tick cap. At the cap, the surviving colonies
are ranked by prestige, then population (units), then finished buildings, and the top one wins.
Prestige comes first because it is the engine's own victory measure. Colonies that stall often
have no prestige at all, and population is then the clearest sign of which one is ahead.
`games.csv` records what decided each capped game (`cap-prestige`, `cap-units`, `cap-buildings` or
`cap-sole-survivor`), and the report also gives the bias counting decisive games only. An exact tie stays unresolved and
is left out of win counts. The report gives the share of games decided at the cap. Placements rank
the winner first, other survivors by prestige, then eliminated colonies, the last eliminated first.

## Reading the summary

`summary.md` (and `summary.json` with every number) has a headline row per generator:

- **Position bias (pp)** is the headline unfairness number. It is the root-mean-square gap between
  each start's true win probability and the fair `1/N`, in percentage points, averaged over the
  generator's maps. Raw win shares scatter around `1/N` even on a perfectly fair map, so the
  estimate is corrected for chance: with `n` decided games and chi-square statistic `X` against
  uniform, `(X - (N - 1)) / ((n - 1) N)` is an unbiased estimate of `sum_s (p_s - 1/N)^2`. Averaged
  over maps, clipped at zero and converted to an RMS, it reads 0 for fair maps. With four colonies,
  one start winning 40% and the others 20% is 8.7 pp. The bracket is a 95% t interval over maps.
  Each map's estimate carries its own game-to-game noise, so the interval covers that noise as well
  as real differences between maps. With only a few maps it is honestly wide.
- **Fair-map floor** is the 95th percentile the position bias reaches when every map is perfectly
  fair, simulated with the same number of decided games per map. Even fair maps show some bias on a
  small sample, so a headline below this floor cannot be told apart from a fair generator. With
  three maps of eight games each the floor is about 14 pp; more games per map lower it.
- **Decisive only** repeats the position bias counting only games won outright, with the number of
  such games, so games decided by tick-cap adjudication cannot drive it.
- **Biased maps** counts maps whose wins by start reject a uniform split at p < 0.05, raw and after
  Benjamini-Hochberg across that generator's maps (false discovery rate 5%). "Chance" is how many
  raw rejections a generator with only fair maps would expect. Holm-adjusted counts are in the
  per-generator section. The p-value is an exact multinomial test (probability of an outcome no
  more likely than the observed one under a uniform split), or a seeded Monte Carlo version when
  there are too many outcomes to enumerate.
- **Best start / fair** is the most-winning start's share times `N`, as a median over maps: 1 is
  fair, 2 means that start won twice its fair share. Picking the best start inflates this on small
  samples, so read it next to the p-value and the position bias. Per map, the table also gives that
  start's share with a Wilson 95% interval.
- **Any bias p** tests whether any of the generator's maps is biased: the sum of the per-map
  chi-square statistics, calibrated by simulating every map's winners under a fair split.
- **Scorer rho** is the rank correlation between a colony's start-quality total and its win share on
  the same map, pooled over maps after centring both within each map. Near +1 the scorer ranks
  starts the way games do; near 0 it does not predict who wins. Its p-value comes from a
  permutation test that shuffles the quality scores among each map's own colonies, which keeps whole
  maps intact and stays valid with few maps; a bootstrap interval over maps appears only from five
  maps up, because fewer cannot support one. The per-generator section adds the correlation with
  placement (every colony's finish, not just the winner's), the correlation for each scoring factor,
  and how often the start the scorer rated best won, against a permutation null that gives each
  map's win counts to a start picked at random. Symmetric arena is left out of the scorer check: its
  colonies are identical by construction, and the small score differences it does show come from the
  build-site count, which is not symmetric under rotation.

The engine team-index table reports wins by team index for the baseline and for all generators
pooled, with an exact test and Wilson intervals.

Per map, the generator section lists wins by start, p and BH q values, the best start's share, the
bias estimate and the start-quality score of each start. Two pooled lines follow: wins by generator
colony index (does the generator favour a colony it places early or late?) and wins by team index.

## Cost

Measured with a release build on an 8-core arm64 Mac shared with another build: four Nicowar
colonies, three games at a time and the default 90,000-tick cap (60 minutes of game time), on one
Symmetric arena and one Contested commons map, every rotation once.

| Map | Games | Wall time per game: mean / median / range | Seconds per 1,000 ticks | Reached the cap |
| --- | ---: | --- | ---: | ---: |
| 128x128 | 8 | 26.9 / 24.1 / 9.3-59.3 s | 0.38 | 4 of 8 |
| 256x256 | 8 | 288.7 / 279.6 / 233.2-357.4 s | 3.21 | 8 of 8 |

Generating a map with its five lobby rolls and every rotation check takes about a second, so games
are the whole cost. A run takes about `games x mean time per game / jobs`:

| Run | Games | Projected wall time at 3 jobs |
| --- | ---: | --- |
| `smoke` (4 generators x 3 maps x 4 rotations x 2 seeds, 128x128) | 96 | about 15 minutes |
| `standard` (15 generators x 12 maps x 4 rotations x 3 seeds, 128x128) | 2,160 | about 5.5 hours |
| `standard --size 256` | 2,160 | about 58 hours |
| `standard --size 256 --map-seeds 2001-2004 --games-per-rotation 1` | 240 | about 6.5 hours |

Game length varies by generator (a map where colonies stall reaches the cap sooner in wall time
than one they build out), and the machine was shared, so treat these as rough.

The presets use 128x128 because at 256x256 no game finished within 90,000 ticks. Every 256x256
result is then an adjudication: colonies were almost all still alive, several often shared the
same prestige, and population decided. A higher `--tick-cap` gets more games won outright, at a
proportional cost in wall time.

## Limits

- The AI is the measuring instrument. A map that is fair for Nicowar can still be unfair for a
  person, and Nicowar's own habits (for example how it expands over water) shape what counts as a
  good start.
- Few games per map give wide intervals. Per-map tests only detect large effects; the generator
  headline, pooled over maps, is the number to act on.
- Cyclic rotations separate the start and team-index main effects, not every interaction between
  them.
- Games stopped at the cap are judged on prestige, which rewards building over military position.
  Check the cap share before reading much into a generator whose games mostly hit it.
