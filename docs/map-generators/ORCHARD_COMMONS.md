# Orchard Commons

Orchard Commons (`orchard-commons`, numeric ID 58, revision 1) is a natural valley
for two to eight colonies. Homes supply survival; separate cherry, orange and prune
groves supply the diets worth fighting over. Claim a grove, establish an inn,
secure another variety, and advertise the inn to tempt hungry enemy workers.

Conversion uses the ordinary game rules. A hungry unit prefers a reachable friendly
inn whose diet matches the best eligible advertised enemy inn. Superior fruit
variety, sufficient wheat, capacity and the conversion cooldown all matter.
Advertising is the player's or AI's decision; generation does not change alliances,
food sharing, unit rules or building statistics. Fruit consumption also retains the
ordinary armour penalty. The map cannot guarantee that an AI chooses conversion.

## Landscape and guarantees

The valley runs along the longer map axis (a seeded choice on square maps). Bending
streams and gravel fords separate the outer banks from the orchard commons. Groves
have irregular outlines, a single fruit kind each, and space to gather and flank
on either side. Two separate 8-by-8 clearings beside each grove accommodate inns,
upgrade margins and circulation. Wheat pockets near the streams support outposts.
The clearings remain ordinary terrain; no buildings are granted.

There are three groves per two colonies, rounded up, with equal counts of each
variety. Fruit order, grove offsets and shapes, river bends, fords, woodland and
home positions vary by seed. Homes are selected near the banks by access to the
three varieties, then dealt to colony indices. They are not surrounded by town
walls or identical cleared plots. Farms and ponds supply renewable wheat and wood;
small quarries provide ordinary upgrade stone. No fruit is planted at home.

On the finished map, first-fruit access from usable swarm exits is 24–48 walking steps; the largest first
access and largest nearest-per-variety access are at most 125% of the smallest.
The validator also checks renewable gathering frontage, home building room,
connected future court approaches, and clear flanking loops around groves.
This is measured access fairness, not tile symmetry or a guarantee of equal wins.

Crops occupy sand-contained plots. Outer woodland is planted only on ground with
zero crop growth probability, so it does not engulf routes; it is finite clearable
scenery. Fruit regrows in place and never spreads. No no-growth flags are used.
Maps retain the normal toroidal wrap and swimming shortcuts.

## Controls and supported requests

- **Orchard spacing:** Compact, Balanced (default), Spread. Offsets between groves
  in each neighborhood are 16, 20 and 24 tiles respectively. This changes the
  distance between varieties without stretching the opening into a long march.
  Because neighborhoods repeat, Spread can also bring neighboring groups closer;
  it is not a uniform dilation of the whole orchard.
- **Wheat, wood, stone, algae and fruit amounts:** ordinary 0–300% controls in 25%
  steps, default 100%. Amounts scale deposits, not the terrain reservations.
- Home wheat, wood and stone have unscaled floors, as do the outpost wheat pockets.
  Stone increases are apportioned across quarries after scaling the map-wide extra
  budget, so each 25% step remains meaningful even with small deposits.
  Every fruit grove retains six fruit tiles at 0%; the default requests 24, rising
  to 60 at 300%, limited by the grove's legal grass. More fruit never grants a
  second variety in the same grove. Algae has no unscaled guarantee.
- Sides must be powers of two from 128 to 512, aspect ratio at most 2:1. Two to four
  colonies can use 128-sized maps. Five to eight require both sides at least 256.
  Invalid requests fail explicitly. Seed-dependent failures are also reported,
  rather than repaired with off-grove fruit or hidden resource rules.

Larger maps retain a compact orchard front and add outer country for building,
clearing and flanking. Irregular lake basins interrupt outer routes, broad sandy
uplands form broad tongues along the valley. Winding grassy swales interrupt the
sand and remain clear of ambient woodland, offering potential building shoulders
and alternate approaches. Periodic terrain fields vary these features across
seeds and wrap continuously. Features fade away from home farms and the central corridor; the
smallest maps can have little or none of this extra terrain. Lakes also supply
ordinary algae, controlled by the algae amount. The surplus land carries no extra
fruit, and only the contained home woodlots provide renewable wood.

See the [landscape gallery and published validation](../artifacts/orchard-commons/README.md).

## Verification

Build and exercise the real-engine mechanism test:

```sh
scons release=1 server=0 -j8 build/src/glob2 orchard-conversion-test map-generator-defaults-test
build/src/OrchardCommonsConversionTest orchard-conversion-test "$PWD" "$PWD/artifacts/orchard-commons/conversion"
build/src/MapGeneratorDefaultsTest orchard-generator-contracts
build/src/glob2 --generate-map orchard-commons --seed 2 --teams 4 --width 256 --height 256 \
  --output artifacts/orchard-commons/example.map --preview artifacts/orchard-commons/example.png \
  --json artifacts/orchard-commons/example.json
```

The conversion harness builds two legal inns on unmodified generated ground,
uses normal building cooldowns and real game ticks, and checks five cases:
superior advertised diet converts; equal friendly diet, no advertising, lost fruit
advantage, and lost wheat do not. Fixtures are saved. Existing load behavior
restarts the food-list conversion cooldown, so the harness waits for eligibility
after loading; it does not claim immediate continuation equivalence.

Native thumbnails omit fruit. Use the terrain dump renderer from the map-design
skill when checking grove shapes and gathering lanes, and inspect a played save
as well as the initial map. The [measurement summary and evidence archive](../artifacts/orchard-commons/README.md) distinguish controlled mechanism tests from
unmodified AI games, seed reliability, and human play assessment.

No engine, save-format, replay or network version changes are introduced. Existing
generators retain their definitions and behavior. Cross-platform simulation checks
and human play review must not be inferred from the local headless evidence.
