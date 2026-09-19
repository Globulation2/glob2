# Hospital shortfalls during attacks — 2026-09-19

The tested surplus-worker tower rule is adopted on `ai/maxima-labour-economy`
in commit `b806198f5`, based on the original `55439d0f1`. The telemetry/rendering/
tournament branches remain in the diagnostic laboratory; they are not merged
into this branch. This adopts the actual tested opportunity-based fallback,
including ordinary priority for later tower repairs/upgrades. It does not imply
that the defense study established a win-rate improvement.

This follow-up measures **192 previously completed games**, not newly simulated
games: 96 with the adopted tower rule and their 96 baseline controls. The 128×128
cohort contains 72 cases (duels and FFA, eight numeric seed blocks); the 256×256
cohort contains 24 FFA cases with 24 new seeds. Cohorts are reported separately.

## Rough size of the shortage

The headline measure is **max(0, wounded warriors − total completed hospital
beds)** at sampled visible colony attacks. Wounded warriors already inside a
hospital are included, so subtracting only *vacant* beds would double-count
those patients. This is a capacity shortfall, not a measured treatment queue.

| Towers enabled | 128×128, duels + FFA | 256×256 FFA |
| --- | ---: | ---: |
| Games with visible colony attack samples | 68 of 72 | 23 of 24 |
| Games with at least one shortage | 51 | 12 |
| Mean per-game fraction of attack samples with a shortage | 32.3% | 26.7% |
| Average missing beds while short, equal weight per affected game | **3.8** | **5.7** |
| Descriptive 95% seed-bootstrap interval for that average | 3.1–4.4 | 4.0–7.4 |
| Median of each affected game's worst sampled shortage | **6** | **11** |
| 90th percentile of those per-game worst shortages | **15** | **20.5** |
| Largest observed shortage | 18 | 22 |

The practical reading is **about 4–6 missing beds during a shortage, with typical
per-game worst spikes around 6–11 and a severe tail around 15–21**. The peak rows
condition on a game having a shortage. They do not imply that every game needs
that much additional capacity. The 90th percentile is interpolated and especially
rough with only twelve affected large-map games.

The baseline controls show a similar scale: average deficits of 3.6 / 6.4 beds,
median affected-game peaks of 5 / 12, and maximum deficits of 20 / 22 on small /
large maps. These descriptive differences are not a paired causal estimate of
the tower effect; policy changes can change which games experience shortages.

## Capacity disappearing matters too

In **28 of 51** affected small-map tower games and **5 of 12** affected large-map
tower games, the first sample attaining the game's maximum shortage had **zero
available hospital beds**. The telemetry does not establish whether that resulted
from destruction, upgrade downtime, retirement or a hospital not yet being built.
Raising a steady-state hospital target alone might not address those events.

Restricting to shortage samples where some beds still existed, the mean per-game
shortfall is **3.3 beds on small maps and 4.4 on large maps**. Median per-game worst
shortfalls within that subset are **4 and 7.5 beds**. This suggests investigating
both the amount of capacity and continuity of service.

Completed hospitals provide **2, 5 and 7 beds** at their three levels (see
`src/game/entities/BuildingTypesColony.cpp`). Six extra beds are equivalent to
three new basic hospitals, or two basic-to-middle upgrades adding three beds
each. These are capacity equivalents, not a construction recommendation:
upgrades remove a hospital's capacity while work is underway, and location and
worker availability matter.

The existing hospital planner aims toward twice current wounded demand. Using
warriors alone, the median affected-game peak gap to that *headroom target* is
**14 beds on smaller maps and 28 on larger maps**. That is not the immediate
shortage. The planner actually counts hurt units of all types, so this warrior-only
calculation can understate its intended headroom requirement.

## Measurement and statistical limits

- Samples are 512 simulation ticks apart. Between-sample peaks can be larger;
  no exact instantaneous maximum or queue waiting time is claimed.
- A pressure sample contains a visible enemy warrior within the same intersection
  of diagnostic and Maxima colony envelopes used in the original defense study.
- Hurt warriors are counted across the team, including attackers away from home.
  Beds are team-wide completed hospital capacity. Other unit types also need
  hospitals but are absent from the wounded-warrior demand count. Reachability,
  treatment speed and distribution between hospitals are not measured here.
- Shortage frequency is averaged per exposed game. Shortage magnitude is first
  averaged within each affected game and then across games. Long battles do not
  get to dominate simply by contributing more samples.
- Descriptive bootstrap intervals resample whole numeric seed blocks (10,000
  deterministic draws), preserving related generators and opponents. They are
  conditional on observed shortage games, not evidence that a specific hospital
  change will improve win rate. Percentiles of game peaks are distribution
  summaries, not confidence intervals. No new policy comparison or tuning occurs.
- When multiple samples share a game's maximum deficit, the first is retained
  for the peak-state fields and the zero-capacity count.

## Per-warrior follow-up and the existing setting

The adopted tower games give the following **equal-weight per-game averages
within shortage samples**. Army size is all living warriors, including wounded
and hospitalized warriors. This is conditional on a shortage, not the normal
healing requirement of every army.

| Per 10 living warriors, during shortages | 128×128 | 256×256 FFA |
| --- | ---: | ---: |
| Wounded warriors | 5.2 | 4.2 |
| Operating hospital beds | 1.5 | 1.4 |
| Missing beds | **3.7** | **2.8** |
| Missing beds when restricting to samples with positive bed capacity | 2.1 | 1.6 |

At each affected game's largest absolute shortage, the median army sizes are
13 and 25.5 warriors respectively. Across *all* attack samples, the average
bed-to-warrior ratios are 0.35 and 0.48, and wounded fractions are 0.28 and 0.22.
Those broader averages hide the timing of the shortages. A universal target
based only on the shortage-conditioned ratios would overstate typical demand.

The configurable `military.hospital_units_per_building` is currently **15**, but
`Labour::hospitalsWorthBuilding` separately enforces **one hospital per eight
warriors**. The director takes the larger target. Consequently, changing 15 to
12, 10 or even 8 cannot increase the target under the current policy. Injury
response can request still more hospitals; both paths use `hospital_cap = 8`.

A small change that actually raises the demographic target would be
`military.hospital_units_per_building = 7`: one per seven rather than one per
eight, about 14% denser before rounding and the cap. For armies of 16, 32, 48
and 64 warriors, the demographic targets would change from 2/4/6/8 hospitals
to 3/5/7/8. Injury-driven demand may already exceed either demographic target.
This is a proposed one-setting experiment, **not an adopted or tested change**.

Basic hospitals provide two beds, so the existing uncapped demographic target
corresponds to approximately 0.25 beds per warrior if all requested hospitals
are basic and completed. At the middle/top hospital levels the same building
count provides 0.625/0.875 beds per warrior. The setting controls buildings,
not seats directly, and upgrades temporarily remove capacity.

During shortages, the observed hospital count (including sites) is below the
existing demographic floor in **55.5% / 65.7%** of samples, averaged per affected
game. It reaches the eight-hospital cap in only **0.3% / 1.4%**. Thus a higher cap
has little direct scope in these observed states. More aggressive demographic
demand could provide a buffer before an attack, but these data also show failure
to deliver or retain capacity already requested by the existing policy. Those
are descriptive observations, not proof that a density change cannot help.

[Per-warrior summaries](per-warrior-summary.json), [game rows](per-warrior-games.csv)
and [sample rows](per-warrior-samples.csv) retain the calculation. Hospital counts
come from same-tick `GLOB2_MEASURE buildings_2_*` fields; even long-level indices
are construction/upgrade sites, odd indices are completed hospitals. Counts do
not include unissued planner reservations. Samples with zero living warriors
are excluded from ratios. No independent-tick significance claim is made.

Reproduce from the complete raw study and the first analysis's retained
`hospital-spikes/pressure-samples.csv`:

```sh
python3 per-warrior.py /path/to/artifacts/defense-study /tmp/hospital-spikes
```

## Evidence and reproduction

[summary.json](summary.json) contains all four cohort/policy summaries and the
largest individual shortages. [games.csv](games.csv) contains one row per game;
[pressure-samples.csv](pressure-samples.csv) contains every relevant observed
state. [capacity-detail.json](capacity-detail.json) separates zero-bed peaks from
shortages with positive capacity. The source commands/maps/results and full
telemetry remain in the original study archive:

`therig.local:/home/bradley/glob2-maxima-defense-study-20260919/artifacts/defense-study`

The original comprehensive report is retained in the separate evidence worktree:
`/Users/bradley/glob2-maxima-defense/docs/validation/maxima-defense/README.md`.
Its experimental classification describes the earlier decision; the tower rule
was subsequently adopted at the user's request, without changing the measured rule.

Run against the complete retained raw study:

```sh
python3 analyze.py /path/to/artifacts/defense-study /tmp/hospital-spikes
python3 capacity-detail.py /tmp/hospital-spikes
```

The source patch adopted here is identical to the candidate that passed combat,
economy, director, lifecycle, placement and strategy-policy regression checks.
The prior clean/lab and macOS/Linux checksum comparisons remain applicable to
that unchanged patch. Full-game continuation mismatches in the diagnostic
baseline and candidate remain unresolved; adoption does not erase that limit.

The adopted branch also builds successfully with `scons -j4 release=1 server=0`;
all twelve strategy-policy tests pass. See [audit.json](audit.json),
[build.log.gz](build.log.gz) and [policy-tests.log](policy-tests.log). The audit
checks 192 unique games and 6,308 unique pressure samples, and confirms the
production sources are identical to the previously validated tower candidate.
