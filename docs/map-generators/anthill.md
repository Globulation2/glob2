# Anthill

Solid rock carved into chambers joined by winding tunnels, with a dirt-track road down every tunnel.
Every colony starts in a queen chamber, a dry cul-de-sac with one door. The other chambers are farm
chambers with a ring of wheat round a pond, plain chambers with a pond and a wheat or wood patch in
turn, dead-end treasure chambers with fruit and wheat, and quarry chambers with a stone deposit. The
rock cannot be walked, built on or cleared, so room is the one scarce thing: a chamber holds a few
buildings, and growing means taking the next chamber down the tunnel.

- **Chambers and tunnels.** Sites `chamber-spacing` apart (20-40, 28; `spreadPoints`), their nearest-site
  cells the graph (`cellGraph` over `siteNeighbours`); a spanning tree through every chamber but the
  queens' (`carveSpanningTree`), one door into each queen chamber, and `loops` percent (0-60, 20) of
  the chambers' count in extra tunnels. Every open edge is a wandering tunnel `tunnel-width` wide as drawn (2-4,
  3; dug half a tile further each side, since the tile beside the rock is drawn half rock) between its chambers' middles (`wanderingPath`), with a dirt-track road traced down its middle that
  stops on the pond's beach at each end (`sand-roads`, on). The road is one vertex wide, so it never
  makes a pure dirt-track tile: the tiles either side are mixed, which walk at grass speed and hold
  no building and no crop, so nothing can be built across a tunnel (first play had AIs walling
  themselves in); every chamber a
  rough disc of `chamber-size` (5-10, 7), farm chambers two bigger, queen chambers three, each turned
  by the golden angle so one outline reads as many. Every chamber's pond is 40% of the chamber's
  radius (a tile more for a farm, half a tile for a queen).
- **Kinds.** Dead ends other than the queens' are treasure chambers; of the rest every other one is a
  farm chamber. One plain chamber in three is a quarry, never the one outside a queen's door and never
  fewer than one per colony while there are candidates. A queen chamber's swarm stands between its
  middle and its door, and the chamber is grown until it holds `queen-room` building sites (20-160,
  60: overlapping 4x4 footprints, the start scorer's measure) with `growUntilSites`, within its own
  cell and never onto a pond's beach, so no colony starts with more room than another. What grows is
  the chamber's core of pure grass; the chamber is that core with a tile of rock face round it.
- **Queen kit.** Wheat and wood on the flanks behind the swarm, a second wheat patch behind it and
  exactly five tiles of stone in a plus towards the back wall, on the nearest ground that holds all
  five (the validator refuses a queen chamber with fewer). None of it regrows and none of it scales with the
  amounts.
- **Quarries.** Dug to a farm chamber's size, with no pond: a stone deposit at the middle, radius 2 at the default `stone-amount`
  (scaled by area, at least 1 above 0%), the roads stopping 4.5 tiles short of it, and up to three
  scree spoil heaps against the walls, each well away from every tunnel's mouth so no haul crosses
  the slow ground. Stone deposits never run out, so a quarry is a permanent supply; the contest is
  over holding one.
- **Rock.** Every vertex of an uncarved tile is ridge rock or outcrop (`ridge-terrain` experiment),
  so an uncarved tile is as impassable as the stone it replaced, and towers still shoot over it.
  Outcrop takes the face where smooth noise is high and thins out behind it, so walls read broken at
  the face and solid within. Open tiles against the face are mixed: walkable, no building, no crop,
  so every chamber is dug a tile wider than its size and keeps the room it had with stone walls.
  `openColonyRoutes` only clears crops in a tunnel; it never cuts rock, and the validator checks that
  the finished terrain is the design's vertex for vertex.
- **Experiments.** A generated map declares the ridge, rough (scree) and path (dirt track) terrain
  experiments in its header.

## Implementation source

[AnthillGenerator.cpp](../../src/map/generator/generators/AnthillGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
