# Solved maps: stating what a map must be true of, and searching for it

Every other generator in this catalog decides what it looks like and then works to make that shape
fair. A *solved* map inverts that: it states properties as numbers it can measure on a candidate
world and searches the arrangements for one that satisfies them. `shared/Solve.h` is the machinery —
annealing with best-state keeping, a Metropolis rule, a named-term `Objective`, and `Brief` for
per-seed targets.

This file is what two of them taught, including the ways they failed. Read it before reaching for a
search; most of it is about *not* reaching for one, or aiming it much more narrowly than feels
natural.

The two maps are **Even Ground** (`even-ground`, searches the terrain itself) and **Marchland**
(`marchland`, draws its country with the ordinary toolkit and searches exactly one decision). They
were built as a deliberate pair so the approach could be judged instead of asserted.

## The headline result: aim it narrowly, and the narrower the better

The pair disagree, and the disagreement is the most useful thing here.

| | What it searches | Static fairness | Fairness tournament |
|---|---|---|---|
| Even Ground | the whole terrain | 0.894 — competitive | **fails**: 25.9 pp position bias, 3 of 6 maps biased, one start won 8 games of 8 |
| Marchland | which sites carry the prizes | 0.890 — competitive | **inconclusive**: 12.2 pp against a fair-map floor of 11.6 |
| Symmetric arena (control) | nothing, it is symmetric | 0.999 | 0.0 pp |

These measurements predate the stream rename and the revision-2 clustering fix; they describe
historical maps. Marchland had no individually significant map after correction, but its pooled
colony-index bias was significant (p 0.018). The sample does not establish fairness or show that
the different search strategies caused the difference. Meanwhile Marchland produces **zero valid maps** without
its search — a rope dealt at random missed its own contest bar on 16 seeds out of 16 — so the search
there is not polish, it is the thing that makes the map possible.

That is the rule: **a search earns its place on a small set of coupled discrete decisions that
construction genuinely cannot reach.** Pointed at terrain it mostly rediscovers noise, and the
result looks it — Even Ground's ground is visibly speckled rather than composed.

Before writing a solver, satisfy all three:

- the property is **exactly measurable** on a candidate, and cheap to measure again;
- it **couples many decisions at once**, so no order of construction gets it right;
- the decisions are **few**, or at least cheap to re-score.

If a property can be built instead, build it. Equal ground per colony is `growTerritories`. An equal
lake apiece is `growLakeBeside`. A search is a worse way to get either, and it will not be
deterministic in the way construction is.

## Optimise what decides games, not what is easy to measure

This is the lesson that cost the most, and it is the one to carry into the next solved map.

Even Ground's objective is *decayed catchment of each crop, equal across colonies*, plus building
room, route width and lake shape. It hits that objective: catchment spread falls from 0.366 to 0.006,
a factor of sixty. Its fairness score says 0.894. And it loses 8 games out of 8 from one start.

The evidence was already in its own telemetry and nobody had read it. On the worst seed the four
colonies' **fitness** — the win-predictive model in `scoreStarts` — came out 0.27 / 0.45 / 0.31 /
0.46, and the 0.46 colony won every game. The same tournament shows that fitness genuinely predicts
wins (rho 0.38 with win share, p 0.020). So the search equalised the quantity it was given while the
quantity that decides games varied freely across the same four colonies, and no check in the design
noticed, because every check was written against the objective.

Three things follow.

1. **Put the predictive measure in the objective.** If `scoreStarts` fitness predicts wins, a map
   claiming fairness should have fitness spread as a cost term, not just the proxy it finds
   convenient. A proxy the search can drive to zero while the real thing stays unequal is worse than
   no target, because it produces a confident number.
2. **A fairness score is not a fairness claim.** `canonical_quality` is a spread over a model. It is
   useful and it is predictive, but it is not a result. Only games are.
3. **Read your own per-colony numbers before believing your aggregate.** The aggregate said fine; the
   per-colony breakdown, printed in the same telemetry, said otherwise.

## Statistical targets have gradients; topological ones are needles

Annealing needs a direction. Every target that works well here is **statistical** — a share, a
ratio, a count, a spread — because moving one cell moves the number readably.

**Topological targets do not work.** "One joined-up channel that crosses the map" has no partial
credit: nine tenths of a river is a lake and a pond, and scores no better than one tenth of one. The
search spends its whole budget wandering.

The river in Marchland is the worked example, and it produced a third category beyond drawn and
searched:

- A **positional** target — "water in the middle" — is satisfied equally well by a blob, a ring, or a
  scatter of ponds with the right centre-weighted share. Nothing in it selects for thin, long or
  joined-up.
- Tighten it until only a one-cell band satisfies it and a channel does appear — **ruler-straight, at
  a fixed offset**, because a band is the only shape the measure named.
- Making it meander means describing the meander, and at that point the route is drawn and the search
  has only coloured it in.

So: **draw the landform, search its placement.** The bed is a closed wandering loop across the torus
— a dozen lines of trigonometry, joined up and the right width by construction, one pass. What is
left over is what construction cannot settle: *which* of fourteen candidate beds to lay given where
the colonies and their farms already are, and where to ford it. Those are few, discrete and exactly
measurable, which is the shape of thing a solver is for.

Note the restraint at the end: fourteen candidates are scored in an exhaustive loop, **not** annealed.
Reaching for a search over fourteen options is the same mistake pointing the other way.

## Invariants versus character

Sort every property into one of two boxes before you write a cost term.

- An **invariant** is what makes the result a map rather than a broken one: everyone joined to
  everyone, ground to build on, a crop in reach. Guarantee these **by construction**, and check them
  in `validateWorld`. Never make them a cost term the search can trade away, and never put them in
  the optional set.
- **Character** is what makes this seed different from the next: how many lakes, how wide the passes,
  whether there is a river at all.

Marchland's river shows both in one feature. That the country stays in one piece is an invariant, so
`fordsToRejoin` guarantees it with a union-find over the banks — no search involved. How many *more*
fords there are beyond that minimum is character, and is what keeps the bed a landform instead of a
wall. Forded only where it must be, the river turned the map into two halves and one colony ended up
contesting five prizes against another's one — the map's own fairness check catching it.

## Draw the brief, or every seed is the same map

With fixed targets, every seed is handed an identical problem, and a search is very good at finding
the same answer to the same question. The layout moves about but the character never does, and twelve
seeds come out as twelve photographs of one map.

`Brief` (in `Solve.h`) is the fix and it does two things:

- **`target(name, least, most)`** draws a per-seed value from a range and records it to telemetry
  automatically. Both ends of the range should be worth playing; where in it a seed lands is the
  map's variety.
- **`choose(optional, least, most)`** picks which optional terms this seed solves to at all. This
  widens variety much further than loosening a target, because a term that is *off* is not a weak
  preference but permission: the search is free to do as it likes in that dimension, and what comes
  out is a different kind of map rather than the same map loosened.

Only ever put character in `choose`. And draw the whole brief in one place at the top of `design()`,
so what a seed was asked for can be read without following the design through.

A slider plus a drawn multiplier is a **band, not a figure**. Even Ground's water slider is multiplied
by that seed's wetness (0.35–2.20), so the slider sets the middle of a band and one seed is a lake
country while the next is dry downland. That is a good design — but say so, because a comment
claiming the value "comes out exact" will be wrong and someone will rely on it.

## Name the objective's terms

Use `Objective`, not a bare summed double. A solved map with one opaque cost can say how badly it did
and never which target it missed, so tuning becomes guesswork: weights get nudged by feel, and a term
that is never the binding one goes on being nudged anyway. Naming costs nothing at run time —
storage is inline and fixed — and turns tuning into evidence, because every map can report per seed
what each target came out at and what each contributed.

It pays immediately:

- **It finds dead terms.** Marchland had a `split` term contributing exactly 8.000 on every seed with
  a river. Root cause was topology: cutting a torus along one non-contractible loop leaves it
  connected, so a single river can never part the map and the term could never discriminate. It was
  recomputing a constant with a full flood per candidate. Deleting it moved no maps.
- **It finds inverted terms.** A bed touching no homeland scored the worst possible 40, because
  `imbalance` returns 1.0 for an all-zero share vector. Found by reading telemetry, not code.

Report both `reportObjective` (every term's residual and contribution) and `reportSolve` (proposals,
kept, cost before and after) for every pass. A map that records these can always answer "what did the
search buy?" for any seed, which is the first question a reviewer should ask of a solved map.

## Annealing practicalities

- **Keep the best state.** A search ends wherever its last accepted move left it, and on a schedule
  even slightly too warm at the end that is not the best place it visited. Before best-keeping existed,
  Even Ground's shape pass finished *worse than it started* on five seeds out of twelve. One snapshot
  per improvement is far rarer than a proposal, so it is nearly free. `anneal` takes `remember()` and
  `recall()` for this.
- **Heat is in the cost's units.** A schedule must be chosen against the cost function it anneals:
  `from` about the size of a typical bad move, `to` small enough that the last moves are strictly
  downhill.
- **`propose()` returning false is not a failure.** It is how a rejection-sampled draw says it found
  nothing this time, and it costs the run only that proposal.
- **Prefer the pass that is cheap to re-score.** Even Ground's shape pass re-measures every colony's
  walk on every move; its stock pass changes each catchment by two known terms and is nearly free. So
  the stock pass gets tens of thousands of moves and does the work that actually equalises the map.
  When a slider is meant to buy fairness, spend it on the cheap pass — as a weight on the expensive
  pass it read as pure noise (0.900 / 0.896 / 0.912 across its whole range).
- **A budget-preserving move keeps a slider honest.** Swapping a water cell with a land cell keeps the
  water count exact, so the slider sets a quantity the search may *arrange* but may not argue with.

## A stream name is part of every draw

`GenerationContext` seeds each named stream from `deriveSeed(seed, name)`, so **renaming a stream
changes every map the generator makes.** This bit twice in one afternoon:

- Renaming the generators late, a guard that protected `"tug-*"` missed the two-hyphen
  `"tug-homes-deal"`. Fourteen of sixteen golden rows changed. The two survivors were the 2-colony
  rows, because `dealStarts` — the thing that stream feeds — is what shuffles colonies onto starts.
- Renaming them deliberately afterwards invalidated every measured number in both design notes and a
  144-game tournament, all of which described maps that no longer existed.

**Rename streams, telemetry keys and controls before the measurement pass, never after.** If you must
rename late, regenerate the golden rows, and treat every figure in the notes as stale until re-taken.

One consolation worth noting: when every map changed, **all 17 contract suites still passed**, because
they assert properties — the envelope, the refusals, a guaranteed lake and quarry per colony, rivers
on some seeds and forded on all of them — rather than fingerprints. A total reshuffle of the maps is a
fair test of whether your checks are really about the design. Write them so they survive it.

## Refusing early beats failing late

A solved map has two ways to say no, and the difference matters to a player.

- `validateRequest` refuses **up front**, before generating, with advice: "use a bigger map or fewer
  colonies". Cheap and actionable.
- `validateWorld` refuses **after** the work, which the lobby absorbs by rerolling, but a player with a
  fixed seed in the editor just sees a failure.

Set the up-front thresholds from where the late failures actually start, measured per cell, not from
where the geometry technically fits. Marchland's floor was 3000 tiles per colony *after* measurement;
at its original 2600 the check passed 128×128 with six colonies and then failed its own validator on
two thirds of seeds. The rates were: about two thirds failing at 2730 tiles a colony, four fifths at
2978, and nought to a third at 3276 — so the floor belongs between them. With that, all 196 attempts
across seven shapes and 2–8 colonies either produced a valid map or were refused up front.

Likewise cap a control where the answers stop improving. Even Ground's water ran to 60 per cent, where
one seed in five refused, the flood saturated near 53 per cent actual water however much more was
asked, and building sites fell from ~13,000 to ~7,700. The range now ends at 40.

## Making the cost function fast

The cost function runs once per proposal, thousands of times, so it is the whole profile. Profile it
directly (`sample` on a loop of generations) rather than guessing. Even Ground went from 297.8 ms to
190.1 ms a map, 36 per cent, with four changes and **no map moved** — golden rows are the check that
an optimisation is really an optimisation.

- **Hoist anything that does not depend on the loop variable.** `widestWalkClearance` begins with
  `clearance(t, mask)`, a full distance transform of the land. The cost function asked for each
  colony's way out in turn, with the same mask, so a four-colony map rebuilt the identical field four
  times per proposal and an eight-colony map eight times. It was 30 per cent of the generator's run
  time. There is now an overload taking a precomputed field.
- **Match the data structure to the key.** The widest-path search used `std::priority_queue`, and two
  fifths of its time was inside `__pop_heap`. The keys are clearances — small bounded integers — so a
  bucket per width walked from the widest down gives the same visit order at O(1) a push and a pop.
- **Hoist reads the compiler cannot.** Two loops reloaded values through vectors it could not prove
  disjoint — `steps[i]` re-read on each of four neighbours because the loop writes `steps[next]`, and a
  scoring loop indexing `walk[k][i]` four times while accumulating into three vector elements.
- **Do not build a mask to answer a constant question.** `stepsFrom(t, source)` — the flood with no
  obstacles — allocated and filled a map-sized all-ones mask per call, then tested it per neighbour.
  Now a compile-time specialisation. Worth 0.71 per cent across all 57 generators, because
  `clearance` is built on it and every passage width in the catalog is measured over `clearance`.
- **Check whether cache is even the problem before optimising for it.** Here it was not: the search
  runs on a 32×32 lattice, so a walk field is 4 KB and the whole per-proposal working set about 25 KB,
  resident in L1 throughout. Shrinking step counts to `int16` would have bought nothing. `Torus::at`
  already takes a compare rather than two divisions on the common in-range step. Measure the working
  set before assuming memory is the constraint.

## Verifying a solved map

In this order, because each step is cheaper than the next and rules out different things.

1. **Golden rows.** `MapGeneratorGoldenTest <profile>` — the map fingerprint per generator, seed and
   shape. This is what says an optimisation or a refactor moved nothing. `--update` after a
   deliberate change, with the revision bumped.
2. **Contract suites.** Assert the design's promises, not fingerprints: the supported envelope, what
   the validator refuses, the guarantees every colony gets. These should survive a total reshuffle of
   the maps.
3. **Telemetry mode.** `--telemetry` checks collection changes nothing and costs little.
4. **The control study.** State per control what should move and which way, *then* read the table —
   see [the tuning playbook](tuning-playbook.md). A report says what moved, not whether the right
   thing moved.
5. **The envelope sweep.** `scripts/sweep_shapes.sh`, every shape against every colony count.
6. **The fairness tournament.** `tools/map_fairness_tournament.py`, with Symmetric arena as the
   control. **This is the only step that would have caught Even Ground.** Everything above it passed.

Report the cost with and without the search, per seed, so its worth can be read rather than taken on
trust. Marchland reports contest and share before and after on every map it makes; Even Ground reports
catchment spread before and after. A solved map that cannot answer "what did the search buy on this
seed?" has not finished.

## A checklist for the next solved map

- [ ] Can this be built instead? Build it.
- [ ] Are the decisions few, discrete, coupled and exactly measurable? If not, do not search.
- [ ] Is the objective the thing that decides games, or a proxy that is merely easy to measure?
- [ ] Is every invariant guaranteed by construction and checked in `validateWorld` — and absent from
      the cost function and the optional set?
- [ ] Is the brief drawn per seed, in one place, and recorded?
- [ ] Are the objective's terms named, and does every term move on some seed?
- [ ] Does the search keep its best state?
- [ ] Are stream names, telemetry keys and control names final *before* the measurement pass?
- [ ] Does an impossible request refuse up front with advice, rather than failing late?
- [ ] Have you played it against Symmetric arena?
