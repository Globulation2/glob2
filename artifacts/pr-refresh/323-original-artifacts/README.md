# Prototype terrain evidence

Previews come from `build/src/glob2 --generate-map <generator> --seed 7 --width 256 --height 256
--teams 4 --set <option>=<value> --preview <file>`:

| Preview | Option |
| --- | --- |
| `preview-watershed-256-frozen-off.png`, `-half-frozen`, `-all-frozen` | `frozen-crossings=0/1/2` |
| `preview-city-states-256-sand-roads.png`, `-cobblestone-roads` | `road-surface=0/1` |
| `preview-fjord-continent-256-ice-bridges.png` | `ice-bridges=1` |
| `preview-old-town-256-cobblestone-streets.png` | `cobblestone-streets=1` |

In previews ice is pale blue and cobblestone tan.

The `game-*.png` screenshots are from the macOS client (`-test-games 1 --map <name> --matchup
castor,castor`) on 128x128 two-colony maps generated with seed 3 and the option on, a few
seconds into the game, halved in size. Old town and City states show Castor building on
cobblestone; the Fjord continent shot is scrolled to an ice bridge still under fog of war, and City states
was generated with seed 3.
`placeholder-tiles.png` shows whole ice and cobblestone tiles and some of their edge tiles over
grass, enlarged.
