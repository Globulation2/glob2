# Watershed

A branching river system draining into a sea along one side of the map: fertile floodplains,
dry sandy uplands, and sand fords across the channels.

- **Layout.** The coast, springs, flow tree and fords are planned in a canonical frame from named
  streams, then oriented: the sea can face any side of a square map but faces a short end of a
  rectangular one, so the rivers run the long way. Each map has one river network.
- **Rivers.** Springs are sampled by land area (`river-density`) and join the network downstream
  of them nearest-first; a path that runs into another river becomes its tributary. Width grows
  with the square root of the springs upstream (`river-width`), and the trunk splits into one to
  three distributaries on a delta lobe (`river-delta`, on by default; off, one mouth and no lobe,
  with the other distributaries still planned so the rest of the network is unchanged). Every
  path meanders (`meanders`, on by default; off, rivers run without their meanders).
- **Channels that keep their water.** Channels are stamped wide enough to keep a four-connected
  water core under the beach rule of `Map::layBeaches()`; `layBeaches()` is then required to
  change nothing.
- **Fords and uplands.** `fords` sets the spacing of sand strips laid across straight stretches
  clear of every other channel. Ground farther from water than `dryness` allows turns to sand,
  never within 11 tiles of water. Springs rise inland, so a ford is the shortest crossing between
  banks, not the only route.
- **Resources and colonies.** Colonies take fertile floodplain sites spread across banks, each
  with an identical wheat, wood and stone kit. Farmland ribbons follow the rivers at 2:1 wheat to
  wood, with stone at the upland edge, fruit at confluences and algae at the mouths.
  `guaranteeStartingResources` runs with 16-step ranges. `validateRequest` allows one colony per
  1,024 map tiles.
- **Checked, not assumed.** `validateWorld` plans the layout again from the request and checks
  that every ford interrupts a real channel, is open across its width and has walkable land at
  both ends; that every channel keeps its water core outside fords; and that every colony can walk
  to colony 0 and stand beside wheat and wood (water, buildings and every resource block).

## Implementation source

[WatershedGenerator.cpp](../../src/map/generator/generators/WatershedGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
