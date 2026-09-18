# Equilibrium: terrain solved against targets instead of drawn

`equilibrium`, legacy id 58. Source: [EquilibriumGenerator.cpp](../../src/map/generator/generators/EquilibriumGenerator.cpp).

Every other landscape in the catalog decides what it looks like and then works to make that shape
fair. This one has no drawn shape at all. It states what the finished map must be true of, and
searches for ground that satisfies it.

## The play contract

Lake country: green plains with a handful of real lakes, no two seeds alike, and no colony better
placed than another in a way a player could read off the map. Because the balance is found by
measurement rather than by mirroring, the map is asymmetric — you cannot infer your opponent's
position from your own — and the ground between colonies tends to be the richest on the map, which
is where the fighting goes.

That last property is not drawn either. The catchment is walking-distance-decayed, so ground far
from everybody costs almost nothing in fairness to leave rich; the search discovers that piling
surplus into the middles is the cheapest way to satisfy its targets.

## The search

A map is far too big to search tile by tile, so the search runs on a lattice of cells 8 to 32 tiles
a side (roughly 16 to 32 cells along the long side, at least 6 along the short one). Each cell is
water, open ground, or a field of one crop. Two annealing passes:

| Pass | Move | Scored on | Cost per move |
|---|---|---|---|
| Shape | swap a water cell with a land cell | equal reachable land, width of the tightest way between neighbours, clear ground near every home, water forming a few lobed bodies, a severe penalty for cutting anyone off | every colony's walk re-measured |
| Stock | swap the contents of two land cells | every colony's decayed catchment of each crop equal, and the worst-served colony's catchment as large as possible | two terms per colony |

Both passes preserve their own budget and search only over arrangement, so a slider sets a quantity
the search may place but may not argue with. The exactness is on the lattice: the finished map
carries somewhat less water than requested, because painting lays a beach along every bank.

The solved lattice is painted through a jittered Voronoi with a noise warp, not as squares.

## What was measured

Canonical quality (`canonical_quality` in the JSON map report), median over 16 seeds at 256×256 with
four colonies, seeds 3001–3016. `fairness` is the report's own colony-fitness fairness; `worst fit`
is the weakest colony's fitness, which is an absolute measure rather than a relative one.

| generator | fairness | win-probability spread | worst fit |
|---|---|---|---|
| symmetric-arena | 0.998 | 0.001 | 0.497 |
| savannah | 0.940 | 0.057 | 0.392 |
| **equilibrium** | **0.930** | **0.065** | **0.204** |
| rain-shadow | 0.927 | 0.063 | 0.139 |
| watershed | 0.824 | 0.163 | 0.155 |
| continents | 0.796 | 0.198 | −0.144 |

So the search beats the asymmetric landscapes it is most like, matches Rain shadow, and does not
reach Savannah — which gets its balance from a designed contained home farm, a much stronger
mechanism at the range that matters most. Exact symmetry remains in a different class, as it should.

The pass that earns this is the stock pass: catchment spread falls from a median 0.366 before it to
0.006 after, a factor of about 60.

Noise floor: three disjoint 16-seed ranges gave fairness 0.884 / 0.894 / 0.890 on one configuration,
so differences under about 0.01 in these tables are not signal.

### Effort, over 16 seeds

| effort | water bodies (asked 2–5) | shore per water cell (asked 0.8) | shape moves kept | fairness | worst fit |
|---|---|---|---|---|---|
| Brief | 6.0 | 1.62 | 302 | 0.892 | 0.186 |
| Normal | 5.0 | 1.31 | 771 | 0.916 | 0.214 |
| Patient | 3.5 | 1.02 | 1801 | 0.913 | 0.221 |

The shape pass does not fully reach its water-shape targets at Normal; Patient nearly does. This is
a budget limit, stated rather than hidden — the telemetry reports target and achieved side by side.

### Balance, over 16 seeds

| balance | 0 | 20 | 40 | 70 (default) | 100 |
|---|---|---|---|---|---|
| fairness | 0.832 | 0.894 | 0.920 | 0.924 | 0.912 |
| worst fit | −0.015 | 0.160 | 0.173 | 0.192 | 0.188 |

## Four things that were tried and did not work

Recorded because each is a trap the next person to touch this will otherwise fall into.

1. **Speckle.** With only the fairness, room and route targets, the search produces a rash of
   single-cell puddles: scattered water satisfies every one of those targets as well as lakes do.
   The shoreline-length target exists to rule that out, and is not a matter of taste.
2. **Uniformity.** Equal catchments are most easily satisfied by making everywhere identical, so the
   maps came out featureless and every seed looked like every other. Two changes fixed it: a short
   catchment decay, which leaves the middles nearly free, and a per-seed target for the number of
   water bodies, which forces large-scale structure the even solution cannot provide.
3. **Plantable doorsteps.** Letting crops sit in the ring of cells around each home looks obviously
   right — a crop at the door is worth most to a catchment. It measured worse on both counts,
   fairness 0.912 → 0.875 and worst fit 0.124 → 0.053, because a cleared doorstep leaves every
   colony taking the same guaranteed opening patch from `secureStartingCrops`. The engine's own crop
   guarantee is the better leveller at that range.
4. **A balance slider that did nothing.** It originally weighted only the shape pass, and read 0.900
   / 0.896 / 0.912 across its whole range — noise. The pass that equalises a map is the stock pass,
   and it ran at full strength whatever the slider said. The slider is now how many moves that pass
   gets.

A fifth finding is about the map rather than the search: cells carrying a crop and how densely each
is planted buy different things. Trading cells for cover (11 per cent of cells at 55 per cent cover)
bought building room at the cost of fairness resolution. Many thin fields gives both.

Water share was set from a sweep over three seed ranges: fairness is flat from 10 to 20 per cent
while the worst colony's building sites fall steadily with every extra lake (about 1470 sites at 10
per cent, 1200 at 20, 750 at 35), so the default is 10.

## Cost and limits

- About 1.0–1.25 s at 256×256 with four colonies at Normal; 3.5 s at 512×512 with eight colonies at
  Patient. This is the slowest generator in the catalog, which is what a search costs. Telemetry
  collection adds about 1.4 per cent.
- Generation succeeds on 24/24 seeds at every shape tested except 512×128 with 8 and with 12
  colonies, which were 23/24; the failing seed is refused by the generator's own validator for a
  colony 25 steps from wheat against a limit of 24. The lobby retries, so this is not player-visible.
- Requests refused up front: fewer than 6 cells on either lattice axis, or fewer than 12 cells per
  colony (a 64-tile map takes at most 5 colonies).
- **Not promised**: equal room to expand into, equal defensibility, or equal contact costs. Only
  catchments, reachable land, building room at home and route width are searched for.

## What has not been done

- No AI games and no fairness tournament. The numbers above are static start quality, which the
  existing study explicitly documents as capable of missing dominant positions.
- No human play, so whether the contested middles actually produce the fights the design expects is
  untested.
- Single-platform (macos-arm64) golden rows only; generator floating-point output is not promised
  identical across platforms, and this generator uses floating-point scoring throughout.
