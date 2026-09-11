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
```

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
  one start winning 40% and the others 20% is 8.7 pp. The bracket is a 95% bootstrap interval over
  maps, so it covers both map-to-map variation and the game-to-game noise within each map.
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
- **Scorer rho** is the rank correlation between a colony's start-quality total and its win share
  on the same map, pooled over maps after centring both within each map, with a bootstrap interval
  over maps. Near +1, the scorer ranks starts the way games do. Near 0, it does not predict who
  wins. The per-generator section adds the correlation with placement (which uses every colony's
  finish, not just the winner's), per scoring factor, and how often the start the scorer rated best
  actually won. Symmetric arena scores every colony alike, so it has no scorer check.

The engine team-index table reports wins by team index for the baseline and for all generators
pooled, with an exact test and Wilson intervals.

Per map, the generator section lists wins by start, p and BH q values, the best start's share, the
bias estimate and the start-quality score of each start. Two pooled lines follow: wins by generator
colony index (does the generator favour a colony it places early or late?) and wins by team index.

## Cost

<!-- COST TABLE -->

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
