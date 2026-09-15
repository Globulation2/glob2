# Emoji

An emoji lagoon or island surrounded by colonies: choose between defending the
outer bypass, taking a crossing into the drawing, and swimming to isolated facial
features. The silhouette supplies varied expansion terrain; the opening economy
comes from crops planted along existing fertile shores.

## Controls

All three variant selectors default to **Random**. Each resolves independently
from a named map-seed stream; the same seed and request reproduce the same result.
The resolved choices appear in the JSON telemetry. Changing abundance does not
reroll the artwork or roads.

| Control | Values |
| --- | --- |
| `character` | 0 Random; 1 Smiley; 2 Sad face; 3 Winking face; 4 Surprised face; 5 Heart eyes; 6 Sunglasses; 7 Heart; 8 Star |
| `outline` | 0 Random; 1 Outline; 2 Filled |
| `inverse` | 0 Random; 1 Regular (ink is water); 2 Inverse (ink is grass) |
| `crossings` | 2, 4 (default), 6, 8 radial connections between bypass and artwork |
| resource amounts | Ambient wheat, wood, stone, algae and fruit; 100% is normal |

Faces retain their eyes and mouths in both styles: these are ink strokes in an
outline and holes in a filled face. Heart and star have hollow outline variants.
Regular outlines divide a grass interior from the surrounding country; crossings
breach the rim. Filled lagoons make shoreline and swimming important. Inverse
filled shapes provide a central continent. Inverse outlines use thicker strokes
(minimum 14 undermap corners) to retain land after beaches, creating narrow fronts.
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
access to features. Flying and swimming units can take additional routes. Some
facial features are deliberately offshore and may need swimming.

The final validator checks every terrain corner against the pre-settlement design,
at least 32 reachable free 4×4 building origins per colony (overlapping anchors,
not 32 buildings), access to both crops, and actual worker connectivity. Resource
obstructions can be cleared along existing land; no emergency terrain is painted.
At non-default resource amounts, the shared cramped-start repair clears deposits
to seek 48 nearby building origins, then rechecks the crop guarantee.
The final minimum remains 32 origins. This addresses crowded high-abundance starts
without changing the standard-abundance maps tested in the balance tournament.
Narrow shorelines offer less expansion room than broad mainland starts. Crops can
spread into initial construction clearings later, so colonies must manage space.

Opening crop targets are equal; natural sites are not exactly symmetric. Emoji faces, hearts and
stars distribute expansion land asymmetrically. Scouting and swimming should
matter, and the bypass should permit a response to losing a crossing. Revise if
home crops cannot sustain a colony, AI placement stalls, the bypass becomes
blocked, the glyph becomes unreadable, or a start repeatedly dominates games.

## Balance validation

Revision 9 retains the opening policy selected through five experimental candidates
and 320 additional AI tournament runs. On three retained problem maps, non-growing
openings before combat fell from 16/96 colonies to 0/96. A separate sixteen-map
fresh-seed check remained at 0/256. These are fixed-map rotations, not independent
samples of every possible map. Gameplay used four colonies/four workers at 256×256
and default abundance; the supported extremes received generation checks.

Nicowar's strongest-position win counts decreased on all three matched full-game
cases. Maxima's late-game balance remains less conclusive: 18/24 revised matches
reached the 90,000-tick cap. Larger populations and longer competing economies are
an intended change in feel; all capped matches still recorded late combat. Human
play and longer matches are needed to judge final resolution and fun.

The local study, rejected candidates, maps and saves are retained under
`artifacts/emoji/balance-tuning/`. Published review artifacts, screenshots and the
complete evidence archive are on the [Emoji evidence branch](https://github.com/Globulation2/glob2/tree/evidence/emoji-map-generator).
The tournament used engine/AI base `b28b4333f`; later AI changes on master were
not part of these measurements. The final
non-default resource repair passed all 64 extreme-setting generation cases and left
the 19 standard-setting gameplay maps byte-identical to the tested candidate.

## Reproduction

```sh
scons release=1 server=0 -j4 build/src/glob2 map-generator-defaults-test map-generator-golden-test
build/src/glob2 --generate-map emoji --seed 7 --set character=1 \
  --set outline=1 --set inverse=1 --output artifacts/emoji/smile.map \
  --preview artifacts/emoji/smile.png --json artifacts/emoji/smile.json
```

Native PNG previews use the game's renderer. Keep request JSON beside saved maps;
the map alone does not retain generator options or internal telemetry. Generated
evidence is collected in `artifacts/emoji/`; attach its evidence bundle to a pull
request for independent review.
