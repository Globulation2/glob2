# Anthill

Solid stone carved into chambers joined by winding tunnels, every chamber with a pond at its middle
and a sand road down every tunnel to the pond at each end. Every colony starts in a queen chamber, a
cul-de-sac with one door; the other chambers are farm chambers with a ring of wheat round the pond,
plain chambers with a wheat or wood patch in turn, or dead-end treasure chambers with fruit and wheat. Stone is everywhere and never runs out, but it cannot be
built on or cleared, so room is the one scarce thing: a chamber holds a few buildings, and growing means
taking the next chamber down the tunnel.

- **Chambers and tunnels.** Sites `chamber-spacing` apart (20-40, 28; `spreadPoints`), their nearest-site
  cells the graph (`cellGraph` over `siteNeighbours`); a spanning tree through every chamber but the
  queens' (`carveSpanningTree`), one door into each queen chamber, and `loops` percent (0-60, 20) of
  the chambers' count in extra tunnels. Every open edge is a wandering tunnel `tunnel-width` wide (2-4,
  3) between its chambers' middles (`wanderingPath`), with a sand road traced down its middle that
  stops on the pond's beach at each end (`sand-roads`, on: a sand corner spoils the tiles round it, so
  nothing can be built across a tunnel; first play had AIs walling themselves in); every chamber a
  rough disc of `chamber-size` (5-10, 7), farm chambers two bigger, queen chambers three, each turned
  by the golden angle so one outline reads as many. Every chamber's pond is 40% of the chamber's
  radius (a tile more for a farm, half a tile for a queen).
- **Kinds.** Dead ends other than the queens' are treasure chambers; of the rest every other one is a
  farm chamber. A queen chamber's swarm stands between its pond and its door, and the chamber is grown
  until it holds `queen-room` building sites (20-160, 60:
  overlapping 4x4 footprints, the start scorer's measure) with `growUntilSites`, within its own cell
  and never onto a pond's beach, so no colony starts with more room than another.
- **Rock.** Stone on every uncarved tile the beaches left pure grass; `openColonyRoutes` clears crops
  in a tunnel and cuts stone only as a last resort, at a cost that keeps it to a tile or two.

## Implementation source

[AnthillGenerator.cpp](../../src/map/generator/generators/AnthillGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
