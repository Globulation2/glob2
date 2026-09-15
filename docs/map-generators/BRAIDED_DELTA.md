# Braided Delta

`braided-delta` (ID 34, revision 3) creates winding river channels linked by repeated,
staggered side channels. The links close loops around elongated islands. There is
no central destination: following a bank leads to several possible crossing
sequences, while swimming can shorten the journey between neighbouring positions.

## Play contract and construction

1. **Budget islands first.** Divide the short map dimension among the requested
   braids. Each lane needs at least `island-size + 16` tiles: the requested land
   budget plus channels, beaches and rasterization margin. Rejoining intervals
   never fall below `max(64, 2 * island-size)` tiles. Reject combinations that
   cannot fit, rather than shrinking colonies or filling channels as a repair.
2. **Close loops on a torus.** Rivers run along the longer dimension; a square
   map randomly chooses horizontal or vertical flow. Shared periodic meanders
   preserve separation and join smoothly across the seam. Side channels shift
   downstream by 45% of an island interval, easing into the adjoining rivers.
   Stagger their attachment stations between lanes so there is no common hub.
3. **Connect the banks.** Propose a ford midway along each side channel and
   regularly along each main channel, with seeded offsets. After beaches, move
   junction-adjacent candidates up to 16 tiles to find two pure-grass endpoints.
   Omit candidates that still cannot fit; never turn a whole reach into a bridge.
   Sand crossings are about four corners wide, providing room for traffic and
   resisting crop spread. Final validation requires every island clearing to be
   reachable, including neutral ones.
4. **Keep useful interiors.** Before furnishing, reserve a 20×20 pure-grass town
   on every island, 25 tiles from its upstream channel. Check its entire footprint
   and surrounding margin before stamping it. If a meander clips a corner, search
   outward in two-tile steps, moving the entire clearing at most six tiles on each
   axis. Never shrink it or fill river water. A two-vertex sand rim separates
   it from spreading crops. Neutral clearings are expansion sites as well as
   alternative colony seats. This regular clearing shape deliberately trades
   some natural appearance for reliable construction and growth containment.
5. **Spread the starts.** Choose island seats by farthest-point selection after
   shuffling candidates, then randomly deal selected sites to teams. One colony
   occupies each chosen island. This reduces crowding and persistent team-index
   bias; it does not establish equal walking distances or competitive balance.
6. **Reserve supplies, then furnish the banks.** Place each home's guaranteed
   wheat/wood kit on the bank before ambient resources can occupy those sites.
   Short ambient wheat/wood patches occupy grass 2–10 steps from water; deeper
   grass receives sparse stone and fruit. Each ambient layer scales
   with its own amount control. Starter wheat and wood patches remain at 0%,
   followed by the shared reachable-resource guarantee. Towns stay out of the
   ambient resource mask. Beaches and town rims are gathering/circulation space;
   initially empty grass outside the rims may grow crops later.
7. **Check the finished world.** Require walking connectivity between colonies,
   unchanged designed grass/water terrain, no wheat or wood inside any reserved town,
   and at least 64 reachable, free 4×4
   building origins on every island. These origins overlap; they do **not** mean
   64 independent buildings. Swarms, resources and units already occupy their
   final positions when room is measured.

### Keeping the approaches open

Initial revision-1 playtesting found an important distinction between a ford and
an accessible ford. In the retained 256×256 seed 51001 Numbi mirror, all four
colonies had armies and attack flags at 45,000 ticks, but all twelve directed
colony pairs were unreachable on foot and there had been no melee combat. Bank
vegetation separated the towns from the otherwise intact crossings. Maxima could
clear routes, which hid the problem in advanced-AI-only testing.

Revision 2 connects both ends of every fitted ford to the protected town rim on
that island with a three-corner-wide sand approach. Island labels are computed
before fords are laid, so the nearest-clearing search cannot cross a river to
service the wrong bank. Shared Dijkstra routing uses cardinal/diagonal costs
10/14 and blocks water and town footprints. Widening never paints water or the
reserved town corners. These paths deliberately cost some farmland and open
several permanent military approaches; they do not change river crossings or
require workers to clear vegetation before an army can leave home.

The regression test fills the grass reachable by existing crops, conservatively
ignoring fertility, then verifies colony connectivity and usable island clearings.
It fails against revision 1 and passes with the approaches. Actual buildings and
combat can still change access later; this is a crop-containment guarantee, not
an unlimited traffic-capacity claim.

These are design heuristics, not established engine survival thresholds. Beaches
can carry units but not buildings. A protected clearing makes room for feeding,
training and swimming infrastructure; army throughput, early pressure and the
value of an outpost still need playtesting. Channels are narrow enough that towers
can threaten parts of an opposing bank: swimming and fords offer movement choices,
not immunity from cross-channel fire.

### Keeping resource repairs out of towns

The revision-2 bulk sweep generated every supported request, but ten extreme-resource
maps invoked the emergency crop guarantee. Seed 711038 (128×128, four colonies,
eight workers, rejoining frequency 3, wheat/stone/fruit 300%, wood/algae 0%) exposed
the consequence: ambient resources occupied the wood kit's bank sites, and the
fallback planted wood inside a protected town. The initial map passed its room and
connectivity checks, but the crop-saturation regression then trapped the colony.

Revision 3 plants guaranteed bank kits before ambient decoration, giving starter
supplies priority without taking town space or changing terrain. The validator also
rejects wheat or wood inside any town plot, even when today's free-building count
looks sufficient. Regression fixtures include the small seed and related 256×256
and 512×512 extremes. The [bulk audit](https://github.com/Globulation2/glob2/blob/evidence/braided-delta-generator/artifacts/braided-delta/pr-assets/README.md)
retains the failure, the corrected sweep, and the supported/invalid request split.

## Initial AI playtesting

These comparisons measured revisions 1 and 2; revision 3 subsequently changed
resource placement order as described above. Paired revision-1/revision-2 games used four colonies at 256×256, game seed 19,
training map seeds 51001–51003 and held-out seeds 52001–52003, capped at 45,000 ticks.
Numbi lost all 72 directed colony walking connections across the six baseline maps;
the revised maps retained all 72. Melee combat occurred in 0/6 baseline games and
4/6 revised games, including all three held-out seeds. A rotated-seat comparison
also retained access after tuning, but had no melee combat in either revision.
Permanent approaches solve the observed crop blockage; they do not ensure prompt AI
attacks. Nicowar reached melee combat in every paired game in both revisions.

The paths have an economy cost: held-out Numbi averages at 15,000 ticks fell from
58 to 53 workers and 33.7 to 29.3 completed buildings across the four teams.
Candidate opening worker starvation was zero in those games. Late Maxima starvation
remained in both versions. Defaults and resource amounts are therefore unchanged;
longer play should assess feeding, outposts and travel pacing before further tuning.

The [retained playtest report](https://github.com/Globulation2/glob2/blob/evidence/braided-delta-generator/artifacts/braided-delta/pr-assets/README.md)
contains the full protocol, paired metrics, caveats, reproduction scripts and links
to saved-world evidence. These are initial AI observations, not proof of human fun
or competitive balance.

## Controls and supported envelope

| Control | Range; default | Effect |
| --- | --- | --- |
| Braid count | 2–5; 2 | Number of longitudinal channels and island lanes |
| Rejoining frequency | 1–3; 2 | Higher values shorten side-channel intervals |
| Island size | 32, 40, 48; 32 | Minimum land budget; larger values also lengthen islands |
| Crossing spacing | 32–96, step 16; 64 | Target spacing along main channels; side-channel fords remain |
| Resource amounts | Shared percentage ranges; 100% | Ambient wheat, wood, stone, algae and fruit; starter crops remain at zero |

Map dimensions must each be at least 128 tiles. Both rectangle orientations work;
maximum dimensions and worker counts follow the shared catalogue (512 and 8).
The exact accepted colony count is at most `braids * intervals`, subject to the
shared team limit. At defaults, 256×256 fits four islands and up to four colonies.
128×128 requires two braids; use rejoining frequency 3 for four colonies.
High braid counts on narrow maps are deliberately rejected. Interval counts are
whole numbers, so nearby settings can produce the same count on a small map.
The final footprint check can reject a seed if its curved shores invade a clearing;
seed/settings sweeps should record those rejections as well as generation failures.

Telemetry records effective island length, channel separation, island count,
orientation, requested/placed crossings, omitted-crossing events, adjusted clearings,
connected ford endpoints, approach sand-corner count, longest approach and ambient tiles.
Shared settlement/resource records explain placement and topups. Instrumentation
adds no RNG draws or terrain analysis when collection is disabled.

## Reproduction and review

```sh
scons release=1 server=0 -j6 build/src/glob2 map-generator-defaults-test map-generator-golden-test
build/src/glob2 --generate-map braided-delta --seed 7 --width 256 --height 256 --teams 4 \
  --output artifacts/braided-delta/seed-7.map \
  --preview artifacts/braided-delta/seed-7.png --json artifacts/braided-delta/seed-7.json
build/src/MapGeneratorDefaultsTest glob2-braided-contracts
build/src/MapGeneratorGoldenTest glob2-braided-golden --require-rows
build/src/MapGeneratorGoldenTest glob2-braided-telemetry --telemetry
```

The implementation and tests are additive. Existing simulation rules, save formats,
replay acceptance and network gates are unchanged. Generation uses the framework's
named streams and inherits its per-platform repeatability contract; cross-platform
map-byte equality is not promised. Retain reports, maps, previews and game evidence
in `artifacts/braided-delta/`; distinguish static generation checks from AI games and
human play. A maintainer should play the map before judging balance or pacing.

### Shared crop containment operations

`cropSeedsIn` in the shared `Growth` module checks reserved tile masks
after resource placement and repairs. Braided Delta applies it to town plots,
whose sand rims already contain outside crops. `cropSpreadEnvelope` supplies the
conservative eight-connected, toroidal crop flood used by the late-growth
regression. It ignores fertility and temporary obstacles: passing establishes
containment even if all connected grass eventually grows crops, rather than
relying on slow growth or repeated harvesting. Both helpers are read-only and
consume no random numbers. The shared toolkit tests sand barriers, diagonal
wrapping, empty seed sets, and wheat/wood versus non-crop deposits.

The completed revision-3 audit generated all 1,509 supported requests and correctly
rejected 50 invalid requests, with no static flags or emergency crop topups. See
[bulk evidence](https://github.com/Globulation2/glob2/blob/evidence/braided-delta-generator/artifacts/braided-delta/pr-assets/README.md) and its
[additional validation](https://github.com/Globulation2/glob2/blob/evidence/braided-delta-generator/artifacts/braided-delta/pr-assets/README.md): nine
selected maps fail the separate full team-rotation byte round-trip audit, which
remains unresolved. Generation success does not certify that boundary.
