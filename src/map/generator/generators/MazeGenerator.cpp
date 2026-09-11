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
#include <utility>
#include <vector>
using namespace MapGeneration;

// A maze in the pen-and-paper sense, built to be lived in. The world is tiled into square cells
// on the map's own torus; a recursive backtracker carves a spanning tree of passages between
// them; every boundary the tree leaves closed becomes a wall - a line of stone in a narrow water
// channel. Passages fill their cells with grass, so colonies farm and build their way out into
// the maze, and every colony starts in its own cul-de-sac.
//
// Terrain is stamped directly rather than through Map::controlSand(), whose in-place raster pass
// shifts shorelines unevenly. Each tile's terrain comes from its four undermap corners
// (Map::regenerateMap), so a pure-grass tile needs grass at all four, and grass must never touch
// water; every grass area here is therefore stamped inside a one-tile sand ring.
namespace {

enum Direction { East, South, West, North };

// Walking out from a cell's centre towards a wall: the passage's grass, then two walkable tiles of
// its sand ring, then channel-width all-water tiles, then the wall - two walkable tiles of flank and
// the stone spine on the boundary itself. Any tile with a non-water corner is walkable, so without
// that water a unit could step onto a wall's flank and follow the wall around the maze; a single
// water tile is enough, since a unit can't step across a tile it can't stand on. The ring, flank
// and spine account for this many tiles of every half-cell.
constexpr int kWallFootprint = 5;

// Half the width of the narrowest passage allowed: a 9-wide chamber still seats the 4x4 swarm
// with room for its workers.
constexpr int kMinimumPassageHalf = 4;

// Every home starts identical: fixed tile counts rather than densities, so the start doesn't
// depend on the resource controls, with wheat and wood 1:1 as for any guaranteed placement.
constexpr int kHomeFarmland = 32;
constexpr int kHomeStone = 12;

// Deposits hug each passage's shores, at most this many tiles in. That leaves a clear lane down
// the middle of every passage however the maze turns, so no deposit can seal a route.
constexpr int kShoreBand = 3;

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
  int cellAt(int x, int y) const {
    const int c = int(std::upper_bound(xs.begin(), xs.end(), x) - xs.begin()) - 1;
    const int r = int(std::upper_bound(ys.begin(), ys.end(), y) - ys.begin()) - 1;
    return std::min(r, rows - 1) * columns + std::min(c, columns - 1);
  }

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

// Passages are as wide as the narrowest cell allows once the wall and its channels are taken
// out, and odd so they centre on a tile.
int passageHalf(const MazeGrid &g, int channelWidth) {
  return g.minimumPitch() / 2 - kWallFootprint - channelWidth;
}

// Whether a set of home cells leaves a valid layout: every home has a non-home neighbour to open
// its one passage into, and the non-home cells form a single connected region for the maze to
// span.
bool homesFit(const MazeGrid &g, const std::vector<unsigned char> &isHome) {
  int start = -1, freeCells = 0;
  for (int cell = 0; cell < g.cells(); ++cell) {
    if (!isHome[cell]) {
      ++freeCells;
      start = cell;
      continue;
    }
    bool opening = false;
    for (int d = 0; d < 4; ++d)
      opening |= !isHome[g.neighbour(cell, d)];
    if (!opening)
      return false;
  }
  if (!freeCells)
    return false;
  std::vector<unsigned char> seen(g.cells(), 0);
  std::vector<int> stack{start};
  seen[start] = 1;
  int reached = 1;
  while (!stack.empty()) {
    const int cell = stack.back();
    stack.pop_back();
    for (int d = 0; d < 4; ++d) {
      const int next = g.neighbour(cell, d);
      if (!isHome[next] && !seen[next]) {
        seen[next] = 1;
        ++reached;
        stack.push_back(next);
      }
    }
  }
  return reached == freeCells;
}

// The home cells as a pattern fixed by the grid alone, so validation checks exactly what
// generation will do: farthest-point spreading on the torus, taking at each step the farthest
// cell that still leaves a valid layout. Empty if the grid can't hold every colony.
std::vector<int> homePattern(const MazeGrid &g, int teams) {
  auto distance = [&](int a, int b) {
    int dx = std::abs(g.column(a) - g.column(b)), dy = std::abs(g.row(a) - g.row(b));
    dx = std::min(dx, g.columns - dx);
    dy = std::min(dy, g.rows - dy);
    return dx * dx + dy * dy;
  };
  std::vector<unsigned char> isHome(g.cells(), 0);
  std::vector<int> homes, nearest(g.cells(), 0);
  for (int team = 0; team < teams; ++team) {
    std::vector<int> order;
    for (int cell = 0; cell < g.cells(); ++cell)
      if (!isHome[cell])
        order.push_back(cell);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return nearest[a] > nearest[b]; });
    int chosen = -1;
    for (int cell : order) {
      isHome[cell] = 1;
      if (homesFit(g, isHome)) {
        chosen = cell;
        break;
      }
      isHome[cell] = 0;
    }
    if (chosen < 0)
      return {};
    homes.push_back(chosen);
    for (int cell = 0; cell < g.cells(); ++cell)
      nearest[cell] = homes.size() == 1 ? distance(cell, chosen)
                                        : std::min(nearest[cell], distance(cell, chosen));
  }
  return homes;
}

std::string validate(const GenerationRequest &r) {
  const MazeOptions o(r);
  const MazeGrid g = mazeGrid(1 << r.wDec, 1 << r.hDec, o.cellSize);
  if (g.columns < 2 || g.rows < 2)
    return "The maze needs at least two cells across and down; use a bigger map or smaller cells.";
  if (passageHalf(g, o.channelWidth) < kMinimumPassageHalf)
    return "Channels this wide leave too little grass in each cell; use narrower channels or "
           "bigger cells.";
  if (int(homePattern(g, r.nbTeams).size()) != r.nbTeams)
    return "The maze has too few cul-de-sacs for this many colonies; use a bigger map or smaller "
           "cells.";
  return "";
}

// The home pattern at a random offset and mirror image on the torus - a translation or reflection
// of a valid layout is still valid - dealt to the colonies in random order.
std::vector<int> chooseHomes(const MazeGrid &g, GenerationContext &context, int teams) {
  std::vector<int> homes = homePattern(g, teams);
  const int offsetX = context.bounded("maze", g.columns);
  const int offsetY = context.bounded("maze", g.rows);
  const bool mirrorX = context.bounded("maze", 2), mirrorY = context.bounded("maze", 2);
  for (int &home : homes) {
    const int c = mirrorX ? (g.columns - g.column(home)) % g.columns : g.column(home);
    const int r = mirrorY ? (g.rows - g.row(home)) % g.rows : g.row(home);
    home = ((r + offsetY) % g.rows) * g.columns + (c + offsetX) % g.columns;
  }
  for (size_t i = homes.size(); i > 1; --i)
    std::swap(homes[i - 1], homes[context.bounded("maze", i)]);
  return homes;
}

// Recursive backtracker (iterative, explicit stack) over the non-home cells: long winding
// passages with comparatively few, deep dead ends - the classic hand-drawn maze texture.
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

// An inclusive rectangle of pure-grass tiles. Stamping one takes undermap grass on
// [x0, x1 + 1] x [y0, y1 + 1] inside undermap sand one line further out.
struct TileRect {
  int x0, y0, x1, y1;
};

void stampRing(Map &map, const TileRect &r) {
  fillUndermap(map, r.x0 - 1, r.y0 - 1, r.x1 - r.x0 + 4, r.y1 - r.y0 + 4, SAND);
}
void stampCore(Map &map, const TileRect &r) {
  fillUndermap(map, r.x0, r.y0, r.x1 - r.x0 + 2, r.y1 - r.y0 + 2, GRASS);
}

// One wall per closed boundary: a stone spine covering every tile of the boundary line from
// corner to corner inclusive, so perpendicular walls share their corner tile and nothing can slip
// between them. As a tile rectangle one tile wide, its pure-grass core is exactly the spine,
// which is what lets STONE be placed on every tile of it.
TileRect wallFor(const MazeGrid &g, int cell, int direction) {
  const int c = g.column(cell), r = g.row(cell);
  if (direction == East)
    return {g.xs[c + 1], g.ys[r], g.xs[c + 1], g.ys[r + 1]};
  return {g.xs[c], g.ys[r + 1], g.xs[c + 1], g.ys[r + 1]};
}

// Distance in 8-neighbour steps from every tile to the nearest tile that isn't pure grass.
std::vector<int> grassDepth(const Map &map) {
  const int w = map.getW(), h = map.getH();
  std::vector<int> depth(size_t(w) * h, -1);
  std::vector<int> queue;
  queue.reserve(size_t(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if (!map.isGrass(x, y)) {
        depth[size_t(y) * w + x] = 0;
        queue.push_back(y * w + x);
      }
  for (size_t head = 0; head < queue.size(); ++head) {
    const int x = queue[head] % w, y = queue[head] / w;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const size_t n = size_t(map.normalizeY(y + dy)) * w + map.normalizeX(x + dx);
        if (depth[n] < 0) {
          depth[n] = depth[size_t(queue[head])] + 1;
          queue.push_back(int(n));
        }
      }
  }
  return depth;
}

struct CellTile {
  int x, y, u, v; // u: towards the cell's exit; v: to the exit's right
};

// Places the first `count` tiles of a region in the order `before` ranks them.
template <typename Order>
void fillInOrder(Map &map, std::vector<CellTile> region, int count, int resourceType,
                 Order before) {
  std::stable_sort(region.begin(), region.end(), before);
  for (int i = 0; i < count && i < int(region.size()); ++i)
    map.setResource(region[i].x, region[i].y, resourceType, 1);
}

// A home's own cell, seen from inside looking out through its exit: wheat banks the left shore
// and wood the right, both growing forward from the dead end's back wall; stone is a compact
// deposit at the back wall's centre (a line along it would read as a second wall). The middle of
// the passage and a square around the swarm stay clear, so workers always walk straight out.
void furnishHome(Map &map, const MazeGrid &g, int cell, int exit, int half) {
  static const int forward[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  const int fx = forward[exit][0], fy = forward[exit][1], rx = -fy, ry = fx;
  const int cx = g.centerX(cell), cy = g.centerY(cell);
  const int lane = std::max(1, half - kShoreBand);
  std::vector<CellTile> left, right, back;
  const int c = g.column(cell), r = g.row(cell);
  for (int y = g.ys[r]; y < g.ys[r + 1]; ++y)
    for (int x = g.xs[c]; x < g.xs[c + 1]; ++x) {
      if (!map.isGrass(x, y) || map.isResource(x, y) || map.getBuilding(x, y) != NOGBID ||
          map.getGroundUnit(x, y) != NOGUID)
        continue;
      const int ox = x - cx, oy = y - cy;
      const int u = ox * fx + oy * fy, v = ox * rx + oy * ry;
      if (std::abs(u) <= 4 && std::abs(v) <= 4)
        continue;
      if (v < -lane)
        left.push_back({x, y, u, v});
      else if (v > lane)
        right.push_back({x, y, u, v});
      else if (u <= -half + 2)
        back.push_back({x, y, u, v});
    }
  const auto shoreFromBack = [](const CellTile &a, const CellTile &b) {
    return a.u != b.u ? a.u < b.u : std::abs(a.v) > std::abs(b.v);
  };
  const auto nearBackCentre = [half](const CellTile &a, const CellTile &b) {
    const int da = std::max(a.u + half, std::abs(a.v)), db = std::max(b.u + half, std::abs(b.v));
    if (da != db)
      return da < db;
    if (std::abs(a.v) != std::abs(b.v))
      return std::abs(a.v) < std::abs(b.v);
    return a.u != b.u ? a.u < b.u : a.v < b.v;
  };
  fillInOrder(map, left, kHomeFarmland, CORN, shoreFromBack);
  fillInOrder(map, right, kHomeFarmland, WOOD, shoreFromBack);
  fillInOrder(map, back, kHomeStone, STONE, nearBackCentre);
}

// A treasure: a compact patch of one fruit at the far end of a dead end, so colonies have to
// go and take it rather than passing it on the way somewhere else. It sits a few tiles back
// from the end wall, so its front always faces the passage's clear lane.
void placeTreasure(Map &map, const MazeGrid &g, int cell, int exit, int half, int size,
                   int fruitType) {
  static const int forward[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  const int fx = forward[exit][0], fy = forward[exit][1], rx = -fy, ry = fx;
  const int cx = g.centerX(cell), cy = g.centerY(cell), anchor = -half + 3;
  std::vector<CellTile> tiles;
  const int c = g.column(cell), r = g.row(cell);
  for (int y = g.ys[r]; y < g.ys[r + 1]; ++y)
    for (int x = g.xs[c]; x < g.xs[c + 1]; ++x)
      if (map.isGrass(x, y) && !map.isResource(x, y)) {
        const int ox = x - cx, oy = y - cy;
        tiles.push_back({x, y, ox * fx + oy * fy, ox * rx + oy * ry});
      }
  fillInOrder(map, tiles, size, fruitType, [anchor](const CellTile &a, const CellTile &b) {
    const int da = std::max(std::abs(a.u - anchor), std::abs(a.v));
    const int db = std::max(std::abs(b.u - anchor), std::abs(b.v));
    if (da != db)
      return da < db;
    if (std::abs(a.v) != std::abs(b.v))
      return std::abs(a.v) < std::abs(b.v);
    return a.u != b.u ? a.u < b.u : a.v < b.v;
  });
}

// Deposits scattered along the shores of every passage outside the homes, as compact clumps
// grown over free shore tiles. Only tiles within kShoreBand of a shore are eligible, which is
// what keeps each passage's middle clear. Densities are per 256 shore tiles.
void scatterThroughMaze(Map &map, GenerationContext &context, const MazeGrid &g,
                        const std::vector<unsigned char> &isHome, int half,
                        const MazeOptions &o) {
  const int w = map.getW(), h = map.getH();
  const std::vector<int> depth = grassDepth(map);
  const int band = std::min(kShoreBand, half - 1);
  std::vector<unsigned char> free(size_t(w) * h, 0);
  std::vector<int> pool;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = size_t(y) * w + x;
      if (depth[i] >= 1 && depth[i] <= band && !isHome[g.cellAt(x, y)] &&
          !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID &&
          map.getGroundUnit(x, y) == NOGUID) {
        free[i] = 1;
        pool.push_back(int(i));
      }
    }
  if (pool.empty())
    return;

  auto clump = [&](int resourceType, int size) {
    int seed = -1;
    for (int attempt = 0; attempt < 32 && seed < 0; ++attempt) {
      const int candidate = pool[context.bounded("resources", pool.size())];
      if (free[candidate])
        seed = candidate;
    }
    if (seed < 0)
      return 0;
    static const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    std::vector<int> frontier{seed};
    free[seed] = 0;
    int placed = 0;
    for (size_t head = 0; head < frontier.size() && placed < size; ++head, ++placed) {
      const int x = frontier[head] % w, y = frontier[head] / w;
      map.setResource(x, y, resourceType, 1);
      for (const auto &s : steps) {
        const size_t n = size_t(map.normalizeY(y + s[1])) * w + map.normalizeX(x + s[0]);
        if (free[n]) {
          free[n] = 0;
          frontier.push_back(int(n));
        }
      }
    }
    for (size_t k = placed; k < frontier.size(); ++k)
      free[frontier[k]] = 1;
    return placed;
  };

  const int shore = int(pool.size());
  for (const auto &layer : {std::pair<int, int>{CORN, o.corn}, std::pair<int, int>{WOOD, o.wood},
                            std::pair<int, int>{STONE, o.stone}}) {
    int remaining = shore * layer.second / 256;
    for (int attempt = 0; remaining > 0 && attempt < shore; ++attempt)
      remaining -=
          clump(layer.first, std::min(remaining, 4 + int(context.bounded("resources", 9))));
  }
}

// Algae is seeded along the channels. They are only a tile or two wide, so any water tile can
// seed a clump: channel water borders a passage's shore, where the algae stays harvestable, and
// algae on water never blocks anyone on foot.
void seedAlgae(Map &map, GenerationContext &context, int algae) {
  const int w = map.getW(), h = map.getH();
  std::vector<MapGeneratorPoint> water;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if (map.isWater(x, y))
        water.emplace_back(x, y);
  if (water.empty())
    return;
  int remaining = int(water.size()) * algae / 400;
  for (int attempt = 0; remaining > 0 && attempt < int(water.size()) / 4 + 16; ++attempt)
    remaining -= placeResourceClump(map, context, water[context.bounded("resources", water.size())],
                                    ALGA, 1);
}

bool generate(Game &game, GenerationContext &context) {
  context.stage = "maze layout";
  const MazeOptions o(context.request);
  Map &map = game.map;
  const MazeGrid g = mazeGrid(map.getW(), map.getH(), o.cellSize);
  const int teams = context.request.nbTeams;
  const int half = passageHalf(g, o.channelWidth);
  map.makeHomogenMap(WATER);
  for (int i = 0; i < teams; ++i)
    game.addTeam();
  if (g.columns < 2 || g.rows < 2 || half < kMinimumPassageHalf) {
    context.detail = "the maze grid is too small for these settings";
    return false;
  }

  const std::vector<int> homes = chooseHomes(g, context, teams);
  if (int(homes.size()) != teams) {
    context.detail = "the maze grid has too few cul-de-sacs for every colony";
    return false;
  }
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
    int choices[4], count = 0;
    for (int d = 0; d < 4; ++d)
      if (!isHome[g.neighbour(home, d)])
        choices[count++] = d;
    const int d = choices[context.bounded("maze", count)];
    open[g.edgeId(home, d)] = 1;
    exitOf[home] = d;
  }
  addLoops(g, context, isHome, o.loopiness, open);
  std::vector<int> deadEnds;
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
      deadEnds.push_back(cell);
      exitOf[cell] = exit;
    }
  }

  // Every cell is a chamber as wide as a passage, and every open boundary a band of that width
  // joining two chambers, so a run of passage reads as one continuous strip of grass.
  context.stage = "maze terrain";
  std::vector<TileRect> passages, walls;
  for (int cell = 0; cell < g.cells(); ++cell) {
    const int cx = g.centerX(cell), cy = g.centerY(cell);
    passages.push_back({cx - half, cy - half, cx + half, cy + half});
    for (int d : {East, South}) {
      if (!open[g.edgeId(cell, d)]) {
        walls.push_back(wallFor(g, cell, d));
        continue;
      }
      const int next = g.neighbour(cell, d);
      if (d == East) {
        const int farX = g.centerX(next) < cx ? g.centerX(next) + g.width : g.centerX(next);
        passages.push_back({cx - half, cy - half, farX + half, cy + half});
      } else {
        const int farY = g.centerY(next) < cy ? g.centerY(next) + g.height : g.centerY(next);
        passages.push_back({cx - half, cy - half, cx + half, farY + half});
      }
    }
  }
  // All sand before any grass, so a grass core is never overwritten by a neighbouring ring.
  for (const TileRect &r : passages)
    stampRing(map, r);
  for (const TileRect &r : walls)
    stampRing(map, r);
  for (const TileRect &r : passages)
    stampCore(map, r);
  for (const TileRect &r : walls)
    stampCore(map, r);
  map.rebuildTerrain();
  for (const TileRect &r : walls)
    for (int y = r.y0; y <= r.y1; ++y)
      for (int x = r.x0; x <= r.x1; ++x) {
        const int nx = map.normalizeX(x), ny = map.normalizeY(y);
        if (map.getTerrainType(nx, ny) != GRASS) {
          context.detail = "a wall spine tile is not solid grass";
          return false;
        }
        map.setResource(nx, ny, STONE, 1);
      }

  for (int team = 0; team < teams; ++team) {
    const int cell = homes[team], c = g.column(cell), r = g.row(cell);
    std::vector<unsigned char> home(size_t(map.getW()) * map.getH(), 0);
    for (int y = g.ys[r]; y < g.ys[r + 1]; ++y)
      for (int x = g.xs[c]; x < g.xs[c + 1]; ++x)
        if (!map.isWater(x, y))
          home[size_t(y) * map.getW() + x] = 1;
    // placeSettlement measures from the footprint's top-left tile; this centres the 4x4 swarm.
    if (!placeSettlement(game, context, team, home, {g.centerX(cell) - 2, g.centerY(cell) - 2},
                         "starts"))
      return false;
  }

  context.stage = "maze resources";
  for (int home : homes)
    furnishHome(map, g, home, exitOf[home], half);
  // Fruit is treasure: one patch at the far end of every dead end that isn't a home, with fruit
  // types dealt round-robin so every kind is somewhere in the maze and a colony has to go and
  // fight for the ones it lacks. Placed before the shore scatter, which works around them.
  if (o.fruit > 0) {
    for (size_t i = deadEnds.size(); i > 1; --i)
      std::swap(deadEnds[i - 1], deadEnds[context.bounded("resources", i)]);
    const int firstType = context.bounded("resources", 3);
    for (size_t i = 0; i < deadEnds.size(); ++i)
      placeTreasure(map, g, deadEnds[i], exitOf[deadEnds[i]], half, o.fruit,
                    CHERRY + int((firstType + i) % 3));
  }
  scatterThroughMaze(map, context, g, isHome, half, o);
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
    : cellSize(r.option("cell-size")), channelWidth(r.option("channel-width")),
      loopiness(r.option("loopiness")), corn(r.option("wheat")), wood(r.option("wood")),
      stone(r.option("stone")), algae(r.option("algae")), fruit(r.option("fruit")) {}

GeneratorDefinition mazeDefinition() {
  return {"maze",
          11,
          "Maze",
          10,
          false,
          {{"cell-size", "Cell size", 24, 48, 1, 32, ControlGroup::Layout, false, false,
            {24, 32, 40, 48}},
           // Open water on each side of a wall's stone line; passages widen to fill the rest.
           {"channel-width", "Channel width", 1, 6, 1, 2, ControlGroup::Layout},
           {"loopiness", "Loopiness", 0, 50, 1, 5, ControlGroup::Layout, false, false,
            {0, 5, 10, 20, 35, 50}},
           // Densities for the deposits scattered along the passages (per 256 shore tiles);
           // homes always get the same fixed amounts. Fruit is the size, in tiles, of the
           // treasure at every dead end that isn't a home.
           {"wheat", "Wheat", 0, 64, 1, 48, ControlGroup::Resources},
           {"wood", "Wood", 0, 64, 1, 24, ControlGroup::Resources},
           {"stone", "Stone", 0, 64, 1, 16, ControlGroup::Resources},
           {"algae", "Algae", 0, 64, 1, 24, ControlGroup::Resources},
           {"fruit", "Fruit", 0, 25, 1, 9, ControlGroup::Resources}},
          generate,
          true,
          validate,
          validateWorld};
}
