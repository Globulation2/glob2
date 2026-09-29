# The Gauntlet

The Gauntlet (`gauntlet`, numeric ID 59, revision 1) is a deliberately constructed
battle arena. Every colony has two guarded entrances, each facing a different
neighbouring court. The courts form a circuit around a sealed central lake, so
armies can advance around the arena without walking through homes. Thin stone
partitions let towers support neighbouring courts while armies use offset gates.
Each home has a compact defended frontage: door angles shrink on sparse large
arenas so supplies and reinforcements do not have to travel around a wide arc.
Holding ground uses ordinary inns, towers and armies; there is no capture mechanic.

Home farms provide renewable wheat and wood. Irrigated court gardens, orchards
containing all three fruits, and clear construction aprons reward expansion.
The lake and the outside coast are sealed with permanent stone: swimming does not
bypass the fronts. Sand lanes and capped beds keep crops off the fighting circuit.

## Controls and supported requests

| Control | Values | Default | Effect |
| --- | --- | --- | --- |
| Court size | 80–120%, steps of 10 | 100 | Expands the court ring radially; trades home farming area for fighting ground. |
| Gate width | 5, 7, 9 tiles | 7 | Widens home approaches and inter-court gates. Wider gates allow more manoeuvring under tower fire. |
| Partition thickness | 1, 2, 3 tiles | 2 | Thickens the stone between courts, increasing separation for fire support. |
| Starting tower level | 0–3 | 1 | None, or one supplied tower at each home entrance at the selected game level. |
| Wheat amount | 0–300% | 100 | Scales extra home wheat and the contested gardens; opening grain remains. |
| Wood amount | 0–300% | 100 | Scales the nearby home woodlot; a minimum opening supply remains. |
| Algae amount | 0–300% | 100 | Scales shallow-water algae, including the home irrigation channels. |
| Fruit amount | 0–300% | 100 | Scales all three orchard fruits in every court; zero removes them. |

Resource amounts are planting requests: wheat can saturate its finite beds at the
highest settings. Even 0% retains opening grain and 30 wood tiles per home; fruit
and algae have no minimum guarantee.

Stone is structural and has no misleading abundance slider. Farm crossings remain
open by construction; allowing them to disappear would undermine this arena's
circulation contract. Towers begin with ammunition and reserve stone. Players
still need the ordinary economy and training to maintain and upgrade them. A
starting tower covers its own entrance's home-side mouth; a wide gate is not a
promise that every tile is under fire.

Maps need a shorter side of at least 256 tiles, 2–12 colonies, at least 48 tiles
of outer-court arc and 27 tiles of inner-court arc per colony, and 22 tiles of
depth behind the home anchor. This permits up to eight colonies at 256×256 and
twelve at 512×512.
Requests that cannot meet these budgets are refused before generation. Rectangles
use a circular arena sized to the shorter side; extra sea does not advantage a
colony. A two-colony game has two different courts between the same opponents.

A named seed stream selects a jousting, bent-wall, or paired-garden court family;
other streams vary global orientation, gate offset, and the deal of home slots to
team indices. Paired gardens fall back to the jousting family when the inner
arc is too narrow; telemetry records the requested and effective family.
This is repeated-wedge fairness, not a claim of exact raster
symmetry. The finished map is checked for resources, building room, both home
exits, every court connection, and permanent circulation after crop spread.

## Verification and reproduction

```sh
scons release=1 server=0 -j8 build/src/glob2 map-generator-defaults-test map-generator-golden-test
build/src/MapGeneratorDefaultsTest gauntlet-contracts --gauntlet-only
build/src/glob2 --generate-map gauntlet --seed 1 --width 256 --height 256 --teams 4 \
  --preview gauntlet.png --json gauntlet.json --output gauntlet.map
```

Keep run-specific review and measurement evidence in the ignored
`artifacts/gauntlet/` workspace or in pull-request attachments. The addition changes no
simulation rules, save format, replay gate, network protocol, or existing generator.
Human play remains necessary to judge pacing and whether the two-front tradeoffs
are enjoyable; AI games establish economy and contact, not human fun.
