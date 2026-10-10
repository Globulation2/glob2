# Emoji

## On this page

- [Controls](#controls)
- [Play contract and limits](#play-contract-and-limits)
- [Balance validation](#balance-validation)
- [Reproduction](#reproduction)

## Controls

All three variant selectors default to **Random**. Each resolves independently
from a named map-seed stream; the same seed and request reproduce the same result.
The resolved choices appear in the JSON telemetry. Changing abundance does not
reroll the artwork or roads.

| Control | Values |
| --- | --- |
| `character` | 0 Random; 1 Smiley; 2 Sad face; 3 Winking face; 4 Surprised face; 5 Heart eyes; 6 Sunglasses; 7 Heart; 8 Star; 9 Neutral face; 10 Sleeping face; 11 Grinning face; 12 Kissing face; 13 Angry face; 14 Tongue out; 15 Skull; 16 Flower; 17 Four-leaf clover; 18 Teardrop; 19 Cloud; 20 Gem; 21 Apple; 22 Speech bubble; 23 Shield |
| `outline` | 0 Random; 1 Outline; 2 Filled |
| `inverse` | 0 Random; 1 Regular (ink is water); 2 Inverse (ink is grass) |
| `crossings` | 2, 4 (default), 6, 8 radial connections between bypass and artwork |
| resource amounts | Ambient wheat, wood, stone, algae and fruit; 100% is normal |

Faces retain their eyes and mouths in both styles: these are ink strokes in an
outline and holes in a filled face. Heart, star, clover, teardrop, cloud and apple are
plain silhouettes with hollow outline variants; the skull, flower centre, gem facets,
speech-bubble dots and shield cross are features like a face's. New characters are
appended to the list, because an explicit `character` value stores its position.

Keep feature land within about 0.55 of the radius, the depth at which a crossing gives
up. The first versions of the sleeping and grinning eyes, the tongue and the gem facets
reached further; a crossing then landed on them and a colony was placed on the cramped
island, failing validation in eight-colony maps. Pulled inside, their failure rate over
16 seeds at 256 and 512 with eight colonies (2.0% and 0.1%) matches the original
characters' (2.5% and 0.2%); with four colonies no character failed. Start placement on
feature islands reached by a crossing is a known limit of every face, not only new ones.
Regular outlines divide a grass interior from the surrounding country; crossings
bridge the rim's water. Filled lagoons make shoreline and swimming important. Inverse
filled shapes provide a central continent. Inverse outlines use thicker strokes
(minimum 14 vertices) to retain land after beaches, creating narrow fronts.
The bypass and crossing causeways deliberately override the binary
artwork before starts are chosen. Beaches are sand in every mode.

## Play contract and limits

Supports square 256×256 and 512×512 maps, 1–8 colonies, and registered worker and
resource ranges. Smaller maps and rectangles are rejected with an explanation.
One colony is useful for exploration; combat requires at least two.

Starts are selected from roomy grass on the largest connected land component,
near a fertile shoreline, then dispersed. Each site can move up to ten tiles per
axis to improve the best 48 fertile grass tiles within twelve tiles of its centre,
with an additional score for local building room. Refinement preserves the legal
town footprint and at least 28 tiles of toroidal separation; sites are then
randomly dealt to teams. The score is a placement heuristic, not a fairness proof. There are
no home pads, private irrigation channels or home approach roads. A circular
six-tile crop exclusion leaves existing grass clear around the starting swarm;
ambient deposits also avoid a ten-tile radius. Neither reservation changes terrain
or growth flags. Shore-farm targets are 48 wheat and 48 wood tiles
per colony, including at 0% ambient abundance. Patches follow the natural shore;
at least 24 of each must fit or generation fails explicitly.

An additional nearby supply targets 24 wheat and 16 wood tiles within ten walking
steps of the starting workers. It can occupy several patches of existing fertile
grass outside the six-tile exclusion. Actual placed counts are recorded in
`emoji.home.close-wheat-placed` and `emoji.home.close-wood-placed`; cramped shores
may fit less. Keeping the shore farms as well as the nearby supplies matters:
replacing productive farms with the nearest available grass caused AI opening
regressions in experiments. The local refinement score is recorded as
`emoji.starts.refinement-score`; `emoji.starts.spacing-tiles` describes dispersion
before that refinement.

The outer sand spine is a continuous loop globally, reachable over existing land from every
colony. It supports two directions of travel around the drawing. Sand stops crop
spread and prevents construction on the spine. Crossings have narrow sand spines
with grass shoulders; a seed changes their placement around the art, altering
access to features. A crossing only bridges water: it runs inward from the bypass to
the first land beyond the first water and stops there, drawing nothing over the art's
own land. A crossing with no water to cross, or one that would only lead out into a
lake (an inverse or filled face's eyes and mouth), is not laid at all, so no single
colony gets a private causeway to a facial island, and the drawing keeps its look
. `emoji.crossings.placed` counts the ones laid. Flying and swimming units can take additional routes. Some
facial features are deliberately offshore and may need swimming.

The final validator checks every terrain corner against the pre-settlement design,
at least 32 reachable free 4×4 building origins per colony (overlapping anchors,
not 32 buildings), access to both crops, and actual worker connectivity. Resource
obstructions can be cleared along existing land; no emergency terrain is painted.
At non-default resource amounts, the shared cramped-start repair clears deposits
to seek 48 nearby building origins, then rechecks the crop guarantee.
The final minimum remains 32 origins. This addresses crowded high-abundance starts
while keeping the standard-abundance layout policy.
Narrow shorelines offer less expansion room than broad mainland starts. Crops can
spread into initial construction clearings later, so colonies must manage space.

Opening crop targets are equal; natural sites are not exactly symmetric. Emoji faces, hearts and
stars distribute expansion land asymmetrically. Scouting and swimming should
matter, and the bypass should permit a response to losing a crossing. Revise if
home crops cannot sustain a colony, AI placement stalls, the bypass becomes
blocked, the glyph becomes unreadable, or a start repeatedly dominates games.

## Balance validation

Opening crop targets and room guards protect the initial economy, but do not
promise equivalent military exposure. Grass-ink outlines can expose rim starts;
enlarging the drawing alone does not address who reaches those starts first. Dry
land outside the bypass has finite supplies, so expansion there can exhaust food.

Check filled, outline, inverse and water-ink variants separately, including complete
seat rotations and resource extremes. Distinguish generation checks from populated
play, and capped matches from decisive outcomes. Human play and long matches are
needed to assess resolution and feel. Historical review artifacts are available on
the [Emoji evidence branch](https://github.com/Globulation2/glob2/tree/evidence/emoji-map-generator);
reproduce new measurements against the current engine and AI rather than treating
those archived cohorts as current balance claims.

## Reproduction

```sh
scons release=1 server=0 -j4 build/src/glob2 engine-tests map-generator-golden-test
build/src/glob2 map generate emoji --seed 7 --set character=1 \
  --set outline=1 --set inverse=1 --output artifacts/emoji/smile.map \
  --preview artifacts/emoji/smile.png --report-file artifacts/emoji/smile.json
```

Native PNG previews use the game's renderer. Keep request JSON beside saved maps;
the map alone does not retain generator options or internal telemetry. Generated
evidence is collected in `artifacts/emoji/`; attach its evidence bundle to a pull
request for independent review.

Related: [map generators](../README.md).
