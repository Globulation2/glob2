# Central Quarry

**Central Quarry** (`central-quarry`, numeric ID 56, revision 1) is natural country of woods, meadows,
lakes and winding streams, all draining to one lake in the middle of the map. In the lake stands an
island, and on the island is the only stone in the world: a small grey outcrop every colony wants and
only one can hold. There are no home plots and no symmetry; colonies start wherever the country lets
them, all at about the same walk from the stone.

## How it plays

- **Stone is the prize.** Stone never runs out and cannot be cleared, so the quarry is permanent and
  its output is limited by how many workers can stand beside it. Every level-0 building costs no stone
  (inns, hospitals, schools, pools, barracks, swarms, and towers themselves), so every colony grows.
  But towers fire stone, and walls, racetracks, markets and every level-1 and level-2 upgrade cost it:
  whoever holds the quarry arms its towers and upgrades its town.
- **The isle is holdable.** It joins the shore only by sand bars, and every bar lands at one place on
  the island, the landing, where the lake is narrowest. A couple of towers at the landing cover every
  way in; towers on the shore opposite still reach it. Each bar's shore end is left clear for an inn.
- **A holder can live there.** A small garden of wheat and wood, sealed by sand against the island's
  far shore so its crops never spread, lets a holder build and feed a garrison on the isle.
- **Swimming is the counter.** A level-0 pool costs no stone, and a swimmer can cross the lake anywhere.
- **Every colony walks as far to the stone.** Starts are spread by walking distance within a narrow
  band of equal walk to the isle (70 steps on 256×256 maps, more with many colonies or on 512), never
  a short swim from it, on watered ground with a similar food yield.

## How it is built

`src/map/generator/generators/CentralQuarryGenerator.cpp`; the header comment and the comments on the
constants record why each choice was made, round by round of review.

| Stage | What happens |
| --- | --- |
| Lake and isle | A rough, stretched lake at the map's middle (radius the lake-size control in tiles, larger on bigger maps, capped on 128), and a rough island drifted along the lake's long axis with a moat at least five corners wide. |
| Bars | The landing is the direction with the shortest crossing from the island's rim to the shore. Every bar starts within a few tiles of it and fans out to shore ends spread either side, a shoal wide at both shores and narrow in the middle with sand islets; a bar that would hug the shore is drawn again closer to the landing. |
| Garden | An oval plot against the island's shore opposite the landing, sealed by a swaying line of sand corners that the shore's beach closes. |
| Streams | One to three streams from the country into the lake along smooth curves through swayed waypoints, widening towards the mouth, with sand fords at uneven spacing; they keep clear of the bars and of one another. |
| Country | Woodland where a noise field biased towards the map's edges is highest (none beside streams), open farmland elsewhere, rough lakes of varied size kept off the prize lake and the streams. |
| Sites | Candidates are roomy grass with a walk to the isle within a band of a target, a straight-line distance at least 80% of it, and preferably watered ground whose 48-step yield is between the 70th and 90th percentile. Sites are spread by walking distance and dealt to colonies. |
| Ponds and territories | Each colony's ground is what it walks to first from its site; a start that is still dry gets one rough pond, kept off every route from the site to the isle. |
| Resources | Biome kits without stone; field edges frayed and bounded by distance from water (never within 20 tiles of a home); an open common round the lake; algae; starter kits facing the nearest water, topped up if crowded. |
| Quarry | A compact outcrop grown in the largest piece of island ground inset from the shore and clear of the garden. |
| Routes | A trail from every colony to the isle (re-cut with less bend, then straight, if it wanders), routes between colonies, the crop guarantee, and removal of one-tile crop slivers; the kits, the garden and the quarry are never cleared. |

## Controls

Effects measured over 6 seeds at 256×256 with 4 colonies (low / default / high), 168 maps.

| Control | Range (default) | Effect |
| --- | --- | --- |
| Lake size | 14–30 (24) | The prize lake's radius in tiles (×1.5 on 512 maps, capped on 128): water 4.1% / 6.2% / 7.6%, island 174 / 438 / 613 tiles. |
| Island size | 30–60 (45) | The island as a share of the lake: 184 / 438 / 802 tiles; a bigger island narrows the landing crossing (9.4 / 6.2 / 4.6 steps). |
| Quarry size | 4–16 (9) | Stone tiles, exactly: the only stone on the map. |
| Sand bars | 1–4 (2) | Bars from the landing, fanned to the shore. |
| Woodland | 0–80 (40) | Wood 4.7% / 11.6% / 19.4%, 4×4 building sites 40.1k / 34.7k / 29.0k. |
| Wheat amount | 0–300% (100) | Wheat 0.3% / 6.2% / 14.3%; starter kits and the island garden are guaranteed. |
| Wood amount | 0–300% (100) | Wood 0.1% / 11.6% / 24.3%. |
| Algae, fruit amount | 0–300% (100) | Linear. |

There is no stone amount: the quarry is the only stone, and `validateWorld` refuses any stone off the isle.

## Verification

- **Reliability.** Every control at every allowed value alone on 128×128, 256×256, 512×512 and
  512×256 with 2, 4 and 8 colonies at two seeds (2,256 maps), plus 1,000 random rolls of every control,
  seven shapes and 2–12 colonies: 3,141 generated and 115 were refusals ("This map has no room for that
  many colonies"), all on maps 128 tiles wide or tall with 8–12 colonies. No validation failures.
- **Every shape.** `sweep_shapes.sh`: refusals only for 64×64 ("The lake does not fit this map"), one
  colony, and crowded maps with a 128-tile side.
- **Golden rows** for macOS and Linux; `MapGeneratorGoldenTest --telemetry` passes (162 cases, 0
  semantic failures).
- **Performance** (CPU seconds per map, mean of 3 seeds): 0.08 at 128×128/4, 0.19 at 256×256/4, 0.84 at
  512×512/8, 1.15 at 512×512/12 (Continents: 0.07, 0.19, 0.78, 0.94). The design is cached between
  generation and validation, and whole-map work repeated per colony was made local.
- **Translations** in every catalog, reviewed by a separate agent.

## Review

Eight rounds with one independent reviewer on frozen builds (2026-09-17). Look and playability out
of 10: 5/5, 6.5/5, 6/5, 6.5/6, 6.5/6.5, 7/6.5, 7.5/7, 8/7.5. The reviewer stopped blocking at round 4.
The findings that changed the design:

- Round 1: sizes as shares of the map put 512 colonies 172 steps from the stone; rivals could start 5
  steps apart; AIs mined at most 4 stone a team. Sizes and walks became tile caps.
- Rounds 2–4: a straight trail cleared one colony's kit (every AI failed that start); edge trims
  starved a seed; the shared Biomes dry reserve dealt its fields from the top rows of the map (fixed at
  the source).
- Round 5: the isle was a shared mine, not a prize (top miner about 45% of the stone). The single
  landing, streams, zoning, shoal bars and, at the maintainer's request, the island garden followed.
- Rounds 6–8: island room, restored water, curved streams, a compact quarry, wider spacing, a garden
  sealed into the shore, and generation made several times faster.

## AI games

Round 8, 256×256 with four colonies, 30,000 ticks, lobby picks of seeds 1–4 (peak units per colony;
stone mined per colony):

| Game | Peak units | Stone | Top miner's share | Top miner biggest | Eliminated |
| --- | --- | --- | --- | --- | --- |
| s1 Nicowar | 44/215/202/86 | 15/53/50/8 | 42% | yes | one |
| s1 Cabino | 48/46/315/48 | 32/28/162/48 | 60% | yes | one |
| s2 Nicowar | 70/77/197/169 | 8/26/47/23 | 45% | yes | – |
| s2 Cabino | 117/61/144/71 | 44/21/251/25 | 74% | yes | – |
| s3 Nicowar | 61/164/91/201 | 17/18/22/40 | 41% | yes | – |
| s3 Cabino | 56/82/106/338 | 43/47/142/52 | 50% | no | one |
| s4 Nicowar | 45/120/194/151 | 7/38/27/14 | 44% | no | – |
| s4 Cabino | 68/58/32/268 | 51/37/22/80 | 42% | yes | – |

Every colony mined stone in every game; the top miner was the biggest colony in six of eight, and
Cabino's holders snowballed to 268–338 units.

## Limits

- **Holdability is untested by AIs.** No AI garrisons the landing; whether a person can hold it with a
  couple of towers, whether swimming is a fair counter and whether the holder's lead feels earned need
  a human playtest.
- **Maxima never mines the quarry.** It counts stone only within 20 tiles of its own buildings
  (`environment.local_territory_radius` in `data/maxima/base.strategy`).
- **Nicowar starves more than Cabino** (up to 80 starvation deaths in a 30,000-tick game) while its
  weak colonies lose the fight for the isle.
- **The ring of equal walk is offset towards the landing**, so colonies can bunch on one side of the
  lake, most visibly on 512×512 with eight colonies.
- **Crowded shapes are refused**: twelve colonies on maps 128 tiles wide, some counts of six to eight on
  128×128 and 128×512, and occasionally twelve on 512×256 ("This map has no room for that many
  colonies").
