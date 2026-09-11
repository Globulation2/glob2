// SPDX-License-Identifier: GPL-3.0-or-later
#include "MazeGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Resources.h"
#include "Settlements.h"
#include "Unit.h"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>
using namespace MapGeneration;

// A maze in the pen-and-paper sense. The world is tiled into square cells on the map's own
// torus; a recursive backtracker carves a spanning tree of narrow corridors between cell
// centres; every boundary the tree leaves closed becomes a wall - a thick water channel with a
// continuous line of stone down its middle. Every colony starts in its own cul-de-sac: a room
// whose only way out is a single corridor.
//
// Terrain is stamped directly rather than through Map::controlSand(), whose in-place raster pass
// shifts shorelines unevenly. Each tile's terrain comes from its four undermap corners
// (Map::regenerateMap), so a pure-grass tile needs grass at all four, and grass must never touch
// water; every grass area here is therefore stamped inside a one-tile sand ring. Corridors are
// sand throughout: walkable, but no crop can ever grow across one, so no corridor can silt shut
// during a game.
namespace {

enum Direction { East, South, West, North };

// Any tile with a non-water corner is walkable, including a wall's sandy flanks. If those flanks
// came near a room or corridor, a unit could step onto them and walk along the wall around the
// maze. Keeping half a cell's pitch at least this much larger than half a room leaves two
// all-water tiles between the room's shore and the wall's.
constexpr int kWallClearance = 7;

// Home rooms are guaranteed placements, so their wheat and wood stay 1:1 regardless of the
// wheat/wood controls, which only shape the bonus rooms at the maze's other dead ends.
constexpr int kHomeFarmland = 40; // of 64

// The cell lattice. Boundaries are integer tile coordinates chosen so the cells tile the map
// exactly, which lets the maze wrap across the torus seam like any other boundary; when
// cell-size doesn't divide the map, neighbouring cells differ in pitch by at most one tile.
struct MazeGrid {
  int width, height, columns, rows;
  std::vector<int> xs, ys;

  int cells() const { return columns * rows; }
  int column(int cell) const { return cell % columns; }
  int row(int cell) const { return cell / columns; }
  int centerX(int cell) const { return (xs[column(cell)] + xs[column(cell) + 1]) / 2; }
  int centerY(int cell) const { return (ys[row(cell)] + ys[row(cell) + 1]) / 2; }
  int minimumPitch() const { return std::min(width / columns, height / rows); }

  int neighbour(int cell, int direction) const {
    const int c = column(cell), r = row(cell);
    switch (direction) {
    case East:
      return r * columns + (c + 1) % columns;
    case South:
      return ((r + 1) % rows) * columns + c;
    case West:
      return r * columns + (c + columns - 1) % columns;
    default:
      return ((r + rows - 1) % rows) * columns + c;
    }
  }
  // Each undirected edge is stored once, owned by its west or north cell.
  int edgeId(int cell, int direction) const {
    switch (direction) {
    case East:
      return 2 * cell;
    case South:
      return 2 * cell + 1;
    case West:
      return 2 * neighbour(cell, West);
    default:
      return 2 * neighbour(cell, North) + 1;
    }
  }
};

MazeGrid mazeGrid(int width, int height, int cellSize) {
  MazeGrid g{width, height, width / cellSize, height / cellSize, {}, {}};
  for (int i = 0; i <= g.columns; ++i)
    g.xs.push_back(i * width / std::max(1, g.columns));
  for (int i = 0; i <= g.rows; ++i)
    g.ys.push_back(i * height / std::max(1, g.rows));
  return g;
}

std::string validate(const GenerationRequest &r) {
  const MazeOptions o(r);
  const MazeGrid g = mazeGrid(1 << r.wDec, 1 << r.hDec, o.cellSize);
  if (g.columns < 3 || g.rows < 3)
    return "The maze needs at least three cells across and down; use a bigger map or smaller cells.";
  if (g.minimumPitch() / 2 - o.roomSize / 2 < kWallClearance)
    return "Rooms this large leave no room for water channels between cells; use smaller rooms "
           "or bigger cells.";
  if (r.nbTeams > (g.columns / 2) * (g.rows / 2))
    return "The maze has too few cul-de-sacs for this many colonies; use a bigger map or smaller "
           "cells.";
  return "";
}

// Home candidates sit on every other column and row, so no two homes touch even diagonally.
// That spacing is what keeps the remaining cells connected (every home is ringed by eight
// non-home cells) and gives every home a non-home neighbour on all four sides to open its one
// corridor into. Among those candidates, homes are spread by farthest-point selection on the
// torus.
std::vector<int> chooseHomes(const MazeGrid &g, GenerationContext &context, int teams) {
  const int offsetX = context.bounded("maze", g.columns);
  const int offsetY = context.bounded("maze", g.rows);
  std::vector<int> candidates;
  for (int j = 0; j < g.rows / 2; ++j)
    for (int i = 0; i < g.columns / 2; ++i)
      candidates.push_back(((offsetY + 2 * j) % g.rows) * g.columns +
                           (offsetX + 2 * i) % g.columns);
  for (size_t i = candidates.size(); i > 1; --i)
    std::swap(candidates[i - 1], candidates[context.bounded("maze", i)]);

  auto distance = [&](int a, int b) {
    int dx = std::abs(g.column(a) - g.column(b)), dy = std::abs(g.row(a) - g.row(b));
    dx = std::min(dx, g.columns - dx);
    dy = std::min(dy, g.rows - dy);
    return dx * dx + dy * dy;
  };
  std::vector<int> homes{candidates[0]};
  std::vector<int> nearest(candidates.size());
  for (size_t i = 0; i < candidates.size(); ++i)
    nearest[i] = distance(candidates[i], homes[0]);
  while (int(homes.size()) < teams) {
    size_t best = 0;
    for (size_t i = 1; i < candidates.size(); ++i)
      if (nearest[i] > nearest[best])
        best = i;
    homes.push_back(candidates[best]);
    for (size_t i = 0; i < candidates.size(); ++i)
      nearest[i] = std::min(nearest[i], distance(candidates[i], candidates[best]));
  }
  return homes;
}

// Recursive backtracker (iterative, explicit stack) over the non-home cells: long winding
// corridors with comparatively few, deep dead ends - the classic hand-drawn maze texture.
bool carveSpanningTree(const MazeGrid &g, GenerationContext &context,
                       const std::vector<unsigned char> &isHome,
                       std::vector<unsigned char> &open) {
  std::vector<int> free;
  for (int cell = 0; cell < g.cells(); ++cell)
    if (!isHome[cell])
      free.push_back(cell);
  if (free.empty())
    return false;
  std::vector<unsigned char> visited(isHome);
  std::vector<int> stack{free[context.bounded("maze", free.size())]};
  visited[stack.back()] = 1;
  while (!stack.empty()) {
    const int cell = stack.back();
    int choices[4], count = 0;
    for (int d = 0; d < 4; ++d)
      if (!visited[g.neighbour(cell, d)])
        choices[count++] = d;
    if (!count) {
      stack.pop_back();
      continue;
    }
    const int d = choices[context.bounded("maze", count)];
    const int next = g.neighbour(cell, d);
    open[g.edgeId(cell, d)] = 1;
    visited[next] = 1;
    stack.push_back(next);
  }
  return std::all_of(visited.begin(), visited.end(), [](unsigned char v) { return v != 0; });
}

// Loopiness knocks through extra walls between non-home cells, giving routes to flank along;
// a home is never touched, so it stays a cul-de-sac.
void addLoops(const MazeGrid &g, GenerationContext &context,
              const std::vector<unsigned char> &isHome, int loopiness,
              std::vector<unsigned char> &open) {
  std::vector<int> closed;
  int freeCells = 0;
  for (int cell = 0; cell < g.cells(); ++cell) {
    if (isHome[cell])
      continue;
    ++freeCells;
    for (int d : {East, South})
      if (!open[g.edgeId(cell, d)] && !isHome[g.neighbour(cell, d)])
        closed.push_back(g.edgeId(cell, d));
  }
  for (size_t i = closed.size(); i > 1; --i)
    std::swap(closed[i - 1], closed[context.bounded("maze", i)]);
  const size_t extra = std::min(closed.size(), size_t(freeCells * loopiness / 100));
  for (size_t i = 0; i < extra; ++i)
    open[closed[i]] = 1;
}

void fillUndermap(Map &map, int x0, int y0, int w, int h, TerrainType t) {
  for (int dy = 0; dy < h; ++dy)
    for (int dx = 0; dx < w; ++dx)
      map.setUMTerrain(map.normalizeX(x0 + dx), map.normalizeY(y0 + dy), t);
}

// A corridor of walkable width 2m+1 centred on the tile row (or column) through both cell
// centres needs sand on the 2m undermap lines from centre-m+1 to centre+m. It is extended past
// each centre by the same amount so corridors meeting at a cell form a square junction.
void carveCorridor(Map &map, const MazeGrid &g, int cell, int direction, int corridorWidth) {
  const int x = g.centerX(cell), y = g.centerY(cell), m = corridorWidth / 2;
  const int next = g.neighbour(cell, direction);
  if (direction == East) {
    int farX = g.centerX(next);
    if (farX < x)
      farX += g.width;
    fillUndermap(map, x - m + 1, y - m + 1, farX - x + 2 * m, 2 * m, SAND);
  } else {
    int farY = g.centerY(next);
    if (farY < y)
      farY += g.height;
    fillUndermap(map, x - m + 1, y - m + 1, 2 * m, farY - y + 2 * m, SAND);
  }
}

// One wall per closed boundary: a stone spine covering every tile of the boundary line from
// corner to corner inclusive, so perpendicular walls share their corner tile and nothing can slip
// between them. The spine sits on a two-wide grass core (so each spine tile is pure grass, which
// STONE requires) inside a sand ring.
struct Wall {
  int x, y, dx, dy, length;
};

Wall wallFor(const MazeGrid &g, int cell, int direction) {
  const int c = g.column(cell), r = g.row(cell);
  if (direction == East)
    return {g.xs[c + 1], g.ys[r], 0, 1, g.ys[r + 1] - g.ys[r]};
  return {g.xs[c], g.ys[r + 1], 1, 0, g.xs[c + 1] - g.xs[c]};
}

struct RoomTile {
  int x, y, u, v; // u: towards the room's exit; v: to the exit's right
};

// The free pure-grass tiles of the room at `cell`, in coordinates relative to its single exit.
// Rooms and corridors are centred on a tile and have odd sizes, so the layout is identical
// whichever way the exit faces.
std::vector<RoomTile> roomTiles(const Map &map, const MazeGrid &g, int cell, int exit,
                                int roomSize) {
  static const int forward[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  const int fx = forward[exit][0], fy = forward[exit][1];
  const int rx = -fy, ry = fx;
  const int cx = g.centerX(cell), cy = g.centerY(cell), half = roomSize / 2;
  std::vector<RoomTile> tiles;
  for (int oy = -half; oy <= half; ++oy)
    for (int ox = -half; ox <= half; ++ox) {
      const int x = map.normalizeX(cx + ox), y = map.normalizeY(cy + oy);
      if (map.isGrass(x, y) && map.getBuilding(x, y) == NOGBID &&
          map.getGroundUnit(x, y) == NOGUID)
        tiles.push_back({x, y, ox * fx + oy * fy, ox * rx + oy * ry});
    }
  return tiles;
}

// Fills `count` tiles of a region, outermost first, so a deposit grows inward from the room's
// walls and the tiles nearest the lane stay free to harvest from.
void fillRegion(Map &map, std::vector<RoomTile> region, int count, int resourceType,
                bool acrossLane) {
  std::stable_sort(region.begin(), region.end(), [&](const RoomTile &a, const RoomTile &b) {
    if (acrossLane)
      return std::abs(a.v) != std::abs(b.v) ? std::abs(a.v) > std::abs(b.v) : a.u < b.u;
    return a.u != b.u ? a.u < b.u : std::abs(a.v) < std::abs(b.v);
  });
  for (int i = 0; i < count && i < int(region.size()); ++i)
    map.setResource(region[i].x, region[i].y, resourceType, 1);
}

// Room layout, seen from inside looking out through the exit: a lane one tile wider on each side
// than the corridor runs from the back wall to the exit and never gets resources, so the exit can
// never be sealed; wheat lines the left wall, wood the right, stone the back wall behind the
// lane. A home also keeps a clear square around its swarm so every worker can walk out.
void furnishRoom(Map &map, GenerationContext &context, const MazeGrid &g, int cell, int exit,
                 const MazeOptions &o, bool home, bool fruit) {
  const int half = o.roomSize / 2, lane = o.corridorWidth / 2 + 1;
  std::vector<RoomTile> left, right, back, fruitRow;
  for (const RoomTile &t : roomTiles(map, g, cell, exit, o.roomSize)) {
    if (home && std::abs(t.u) <= 4 && std::abs(t.v) <= 4)
      continue;
    if (t.v < -lane)
      left.push_back(t);
    else if (t.v > lane)
      right.push_back(t);
    else if (t.u <= -half + 1)
      back.push_back(t);
    else if (fruit && t.u == -half + 2 && std::abs(t.v) <= 1)
      fruitRow.push_back(t);
  }
  if (home) {
    const int farmland = int(std::min(left.size(), right.size())) * kHomeFarmland / 64;
    fillRegion(map, left, farmland, CORN, true);
    fillRegion(map, right, farmland, WOOD, true);
  } else {
    fillRegion(map, left, int(left.size()) * o.corn / 64, CORN, true);
    fillRegion(map, right, int(right.size()) * o.wood / 64, WOOD, true);
  }
  fillRegion(map, back, int(back.size()) * o.stone / 64, STONE, false);
  if (!fruitRow.empty())
    fillRegion(map, fruitRow, int(fruitRow.size()), CHERRY + context.bounded("resources", 3),
               false);
}

// Algae belongs in the channels, not the corridors: clumps are seeded only in open water at
// least two tiles from any land, where they read as part of the moat and are still reachable
// from the nearest shore.
void seedAlgae(Map &map, GenerationContext &context, int algae) {
  const int w = map.getW(), h = map.getH();
  std::vector<MapGeneratorPoint> openWater;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      bool clear = true;
      for (int dy = -2; dy <= 2 && clear; ++dy)
        for (int dx = -2; dx <= 2 && clear; ++dx)
          clear = map.isWater(map.normalizeX(x + dx), map.normalizeY(y + dy));
      if (clear)
        openWater.emplace_back(x, y);
    }
  if (openWater.empty())
    return;
  int remaining = int(openWater.size()) * algae / 1600;
  for (int attempt = 0; remaining > 0 && attempt < 4 * int(openWater.size()) / 25 + 16;
       ++attempt)
    remaining -= placeResourceClump(map, context,
                                    openWater[context.bounded("resources", openWater.size())],
                                    ALGA, 1);
}

bool generate(Game &game, GenerationContext &context) {
  context.stage = "maze layout";
  const MazeOptions o(context.request);
  Map &map = game.map;
  const MazeGrid g = mazeGrid(map.getW(), map.getH(), o.cellSize);
  const int teams = context.request.nbTeams;
  map.makeHomogenMap(WATER);
  for (int i = 0; i < teams; ++i)
    game.addTeam();
  if (g.columns < 3 || g.rows < 3 || teams > (g.columns / 2) * (g.rows / 2)) {
    context.detail = "the maze grid has too few cul-de-sacs for every colony";
    return false;
  }

  const std::vector<int> homes = chooseHomes(g, context, teams);
  std::vector<unsigned char> isHome(g.cells(), 0);
  for (int home : homes)
    isHome[home] = 1;
  std::vector<unsigned char> open(2 * g.cells(), 0);
  if (!carveSpanningTree(g, context, isHome, open)) {
    context.detail = "the non-home cells did not form one connected maze";
    return false;
  }
  std::vector<int> exitOf(g.cells(), -1);
  for (int home : homes) {
    const int d = context.bounded("maze", 4);
    open[g.edgeId(home, d)] = 1;
    exitOf[home] = d;
  }
  addLoops(g, context, isHome, o.loopiness, open);
  std::vector<int> bonusRooms;
  for (int cell = 0; cell < g.cells(); ++cell) {
    if (isHome[cell])
      continue;
    int degree = 0, exit = -1;
    for (int d = 0; d < 4; ++d)
      if (open[g.edgeId(cell, d)]) {
        ++degree;
        exit = d;
      }
    if (degree == 1) {
      bonusRooms.push_back(cell);
      exitOf[cell] = exit;
    }
  }

  // All sand before any grass, so a grass core is never overwritten by a neighbouring ring.
  context.stage = "maze terrain";
  const int half = o.roomSize / 2;
  std::vector<Wall> walls;
  for (int cell = 0; cell < g.cells(); ++cell)
    for (int d : {East, South}) {
      if (open[g.edgeId(cell, d)])
        carveCorridor(map, g, cell, d, o.corridorWidth);
      else
        walls.push_back(wallFor(g, cell, d));
    }
  for (int cell = 0; cell < g.cells(); ++cell)
    if (exitOf[cell] >= 0)
      fillUndermap(map, g.centerX(cell) - half - 1, g.centerY(cell) - half - 1, 2 * half + 4,
                   2 * half + 4, SAND);
  for (const Wall &w : walls)
    fillUndermap(map, w.x - 1, w.y - 1, w.dx ? w.length + 4 : 4, w.dy ? w.length + 4 : 4, SAND);
  for (int cell = 0; cell < g.cells(); ++cell)
    if (exitOf[cell] >= 0)
      fillUndermap(map, g.centerX(cell) - half, g.centerY(cell) - half, 2 * half + 2,
                   2 * half + 2, GRASS);
  for (const Wall &w : walls)
    fillUndermap(map, w.x, w.y, w.dx ? w.length + 2 : 2, w.dy ? w.length + 2 : 2, GRASS);
  map.rebuildTerrain();
  for (const Wall &w : walls)
    for (int i = 0; i <= w.length; ++i) {
      const int x = map.normalizeX(w.x + i * w.dx), y = map.normalizeY(w.y + i * w.dy);
      if (map.getTerrainType(x, y) != GRASS) {
        context.detail = "a wall spine tile is not solid grass";
        return false;
      }
      map.setResource(x, y, STONE, 1);
    }

  for (int team = 0; team < teams; ++team) {
    const int cx = g.centerX(homes[team]), cy = g.centerY(homes[team]);
    std::vector<unsigned char> home(size_t(map.getW()) * map.getH(), 0);
    for (int oy = -half - 2; oy <= half + 2; ++oy)
      for (int ox = -half - 2; ox <= half + 2; ++ox) {
        const int x = map.normalizeX(cx + ox), y = map.normalizeY(cy + oy);
        if (!map.isWater(x, y))
          home[size_t(y) * map.getW() + x] = 1;
      }
    // placeSettlement measures from the footprint's top-left tile; this centres the 4x4 swarm.
    if (!placeSettlement(game, context, team, home, {cx - 2, cy - 2}, "starts"))
      return false;
  }

  context.stage = "maze resources";
  for (int home : homes)
    furnishRoom(map, context, g, home, exitOf[home], o, true, false);
  std::vector<unsigned char> hasFruit(g.cells(), 0);
  for (int i = 0; i < o.fruit && i < int(bonusRooms.size()); ++i) {
    const size_t pick = i + context.bounded("resources", bonusRooms.size() - i);
    std::swap(bonusRooms[i], bonusRooms[pick]);
    hasFruit[bonusRooms[i]] = 1;
  }
  for (int cell : bonusRooms)
    furnishRoom(map, context, g, cell, exitOf[cell], o, false, hasFruit[cell]);
  seedAlgae(map, context, o.algae);
  return true;
}

// The maze's defining guarantee, checked on the finished world rather than trusted: every
// colony can walk to every other one. Water, buildings and every resource (including the wall
// spines) block the flood; units don't, since they move.
std::string validateWorld(const Game &game, const GenerationContext &context) {
  const Map &map = game.map;
  const int w = map.getW(), h = map.getH(), teams = context.request.nbTeams;
  std::vector<unsigned char> reached(size_t(w) * h, 0);
  std::vector<int> queue;
  queue.reserve(size_t(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const Uint16 gid = map.getGroundUnit(x, y);
      if (gid != NOGUID && Unit::GIDtoTeam(gid) == 0) {
        reached[size_t(y) * w + x] = 1;
        queue.push_back(y * w + x);
      }
    }
  for (size_t head = 0; head < queue.size(); ++head) {
    const int x = queue[head] % w, y = queue[head] / w;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
        const size_t n = size_t(ny) * w + nx;
        if (!reached[n] && !map.isWater(nx, ny) && !map.isResource(nx, ny) &&
            map.getBuilding(nx, ny) == NOGBID) {
          reached[n] = 1;
          queue.push_back(int(n));
        }
      }
  }
  std::vector<unsigned char> teamReached(teams, 0);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const Uint16 gid = map.getGroundUnit(x, y);
      if (gid != NOGUID && reached[size_t(y) * w + x] && Unit::GIDtoTeam(gid) < teams)
        teamReached[Unit::GIDtoTeam(gid)] = 1;
    }
  for (int team = 0; team < teams; ++team)
    if (!teamReached[team])
      return "Colony " + std::to_string(team) + " cannot walk to colony 0 through the maze.";
  return "";
}
} // namespace

MazeOptions::MazeOptions(const GenerationRequest &r)
    : cellSize(r.option("cell-size")), roomSize(r.option("room-size")),
      corridorWidth(r.option("corridor-width")), loopiness(r.option("loopiness")),
      corn(r.option("wheat")), wood(r.option("wood")), stone(r.option("stone")),
      algae(r.option("algae")), fruit(r.option("fruit")) {}

GeneratorDefinition mazeDefinition() {
  return {"maze",
          11,
          "Maze",
          7,
          false,
          {{"cell-size", "Cell size", 24, 48, 1, 32, ControlGroup::Layout, false, false,
            {24, 32, 40, 48}},
           // Room and corridor widths are walkable tiles and always odd, so both centre on a tile.
           {"room-size", "Room size", 9, 17, 2, 15, ControlGroup::Layout},
           {"corridor-width", "Corridor width", 3, 7, 1, 5, ControlGroup::Layout, false, false,
            {3, 5, 7}},
           {"loopiness", "Loopiness", 0, 50, 1, 10, ControlGroup::Layout, false, false,
            {0, 5, 10, 20, 35, 50}},
           // Wheat and wood shape only the bonus rooms (homes are fixed at 1:1); fruit is a
           // count of fruit patches, one per bonus room at most.
           {"wheat", "Wheat", 0, 64, 1, 48, ControlGroup::Resources},
           {"wood", "Wood", 0, 64, 1, 24, ControlGroup::Resources},
           {"stone", "Stone", 0, 64, 1, 32, ControlGroup::Resources},
           {"algae", "Algae", 0, 64, 1, 24, ControlGroup::Resources},
           {"fruit", "Fruit", 0, 16, 1, 4, ControlGroup::Resources}},
          generate,
          true,
          validate,
          validateWorld};
}
