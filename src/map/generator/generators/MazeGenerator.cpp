// SPDX-License-Identifier: GPL-3.0-or-later
#include "MazeGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Resources.h"
#include "Settlements.h"
#include "Topology.h"
#include <algorithm>
#include <utility>
using namespace MapGeneration;
namespace {
std::string validate(const GenerationRequest &r) {
  const MazeOptions o(r);
  if (o.roomSize + 2 > o.cellSize)
    return "Room size must not exceed cell size.";
  if (o.corridorWidth + 2 > o.cellSize)
    return "Corridor width must not exceed cell size.";
  const int cells = (1 << r.wDec) / o.cellSize * ((1 << r.hDec) / o.cellSize);
  return cells < r.nbTeams ? "The map needs at least one maze room per colony."
                           : "";
}

bool generate(Game &game, GenerationContext &context) {
  context.stage = "maze layout";
  const MazeOptions o(context.request);
  const int width = game.map.getW(), height = game.map.getH();
  const int columns = width / o.cellSize, rows = height / o.cellSize;
  const int cellCount = columns * rows;
  game.map.makeHomogenMap(WATER);
  for (int i = 0; i < context.request.nbTeams; ++i)
    game.addTeam();

  std::vector<std::pair<int, int>> edges;
  // Shore generation consumes the outer terrain tile. The room and corridor
  // controls describe the grass remaining after that shoreline is built.
  const int rawRoomSize = o.roomSize + 2;
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < columns; ++x) {
      const int cell = y * columns + x;
      if (x + 1 < columns)
        edges.emplace_back(cell, cell + 1);
      if (y + 1 < rows)
        edges.emplace_back(cell, cell + columns);
    }
  for (size_t i = edges.size(); i > 1; --i)
    std::swap(edges[i - 1], edges[context.bounded("maze", i)]);

  std::vector<int> parent(cellCount);
  for (int i = 0; i < cellCount; ++i)
    parent[i] = i;
  auto root = [&](int a) {
    int r = a;
    while (parent[r] != r)
      r = parent[r];
    while (parent[a] != a) {
      const int next = parent[a];
      parent[a] = r;
      a = next;
    }
    return r;
  };
  std::vector<unsigned char> connected(edges.size(), 0);
  int treeEdges = 0;
  for (size_t i = 0; i < edges.size() && treeEdges < cellCount - 1; ++i) {
    const int a = root(edges[i].first), b = root(edges[i].second);
    if (a != b) {
      parent[a] = b;
      connected[i] = 1;
      ++treeEdges;
    }
  }
  int loops =
      std::min(cellCount * o.loopiness / 100, int(edges.size()) - treeEdges);
  for (size_t i = 0; i < edges.size() && loops; ++i)
    if (!connected[i]) {
      connected[i] = 1;
      --loops;
    }

  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < columns; ++x) {
      const int cx = x * o.cellSize + o.cellSize / 2;
      const int cy = y * o.cellSize + o.cellSize / 2;
      for (int dy = 0; dy < rawRoomSize; ++dy)
        for (int dx = 0; dx < rawRoomSize; ++dx)
          game.map.setUMTerrain(cx - rawRoomSize / 2 + dx,
                                cy - rawRoomSize / 2 + dy, GRASS);
    }
  for (size_t i = 0; i < edges.size(); ++i)
    if (connected[i]) {
      const int a = edges[i].first, b = edges[i].second;
      const int x0 = (a % columns) * o.cellSize + o.cellSize / 2;
      const int y0 = (a / columns) * o.cellSize + o.cellSize / 2;
      const int x1 = (b % columns) * o.cellSize + o.cellSize / 2;
      const int y1 = (b / columns) * o.cellSize + o.cellSize / 2;
      const int half = o.corridorWidth / 2 + 1;
      for (int y = std::min(y0, y1) - half; y <= std::max(y0, y1) + half; ++y)
        for (int x = std::min(x0, x1) - half; x <= std::max(x0, x1) + half; ++x)
          game.map.setUMTerrain(x, y, GRASS);
    }

  // Closed cell edges are real maze walls: a grass seam carrying stone, with
  // shoreline and water on both sides. Stone blocks ground units as well as
  // swimmers; open tree/loop edges remain gaps through the wall grid.
  std::vector<unsigned char> stoneWall(size_t(width) * height, 0);
  for (size_t i = 0; i < edges.size(); ++i)
    if (!connected[i]) {
      const int a = edges[i].first, b = edges[i].second;
      const int ax = (a % columns) * o.cellSize + o.cellSize / 2;
      const int ay = (a / columns) * o.cellSize + o.cellSize / 2;
      const int bx = (b % columns) * o.cellSize + o.cellSize / 2;
      const int by = (b / columns) * o.cellSize + o.cellSize / 2;
      if (ay == by) {
        const int wallX = (ax + bx) / 2;
        for (int y = ay - o.cellSize / 2; y <= ay + o.cellSize / 2; ++y) {
          for (int dx = -1; dx <= 1; ++dx)
            game.map.setUMTerrain(wallX + dx, y, GRASS);
          stoneWall[game.map.normalizeY(y) * width +
                    game.map.normalizeX(wallX)] = 1;
        }
      } else {
        const int wallY = (ay + by) / 2;
        for (int x = ax - o.cellSize / 2; x <= ax + o.cellSize / 2; ++x) {
          for (int dy = -1; dy <= 1; ++dy)
            game.map.setUMTerrain(x, wallY + dy, GRASS);
          stoneWall[game.map.normalizeY(wallY) * width +
                    game.map.normalizeX(x)] = 1;
        }
      }
    }
  game.map.controlSand();
  game.map.rebuildTerrain();

  RegionGraph graph(cellCount);
  for (size_t i = 0; i < edges.size(); ++i)
    if (connected[i]) {
      graph[edges[i].first].push_back(edges[i].second);
      graph[edges[i].second].push_back(edges[i].first);
    }
  std::vector<int> homes{0};
  std::vector<unsigned char> chosen(cellCount, 0);
  chosen[0] = 1;
  for (int team = 1; team < context.request.nbTeams; ++team) {
    const auto distances = graphDistances(graph, homes);
    int best = -1;
    for (int cell = 0; cell < cellCount; ++cell)
      if (!chosen[cell] && (best < 0 || distances[cell] > distances[best]))
        best = cell;
    if (best < 0)
      return false;
    chosen[best] = 1;
    homes.push_back(best);
  }

  std::vector<std::vector<MapGeneratorPoint>> teamWheatAreas, teamWoodAreas;
  for (int team = 0; team < context.request.nbTeams; ++team) {
    const int cell = homes[team];
    const int cx = (cell % columns) * o.cellSize + o.cellSize / 2;
    const int cy = (cell / columns) * o.cellSize + o.cellSize / 2;
    std::vector<unsigned char> home(size_t(width) * height, 0);
    std::vector<MapGeneratorPoint> wheatArea, woodArea;
    for (int dy = -1; dy < rawRoomSize + 1; ++dy)
      for (int dx = -1; dx < rawRoomSize + 1; ++dx) {
        const int x = game.map.normalizeX(cx - rawRoomSize / 2 + dx);
        const int y = game.map.normalizeY(cy - rawRoomSize / 2 + dy);
        if (!game.map.isWater(x, y)) {
          home[y * width + x] = 1;
          if (game.map.isGrass(x, y) && dx < rawRoomSize / 3)
            wheatArea.emplace_back(x, y);
          if (game.map.isGrass(x, y) && dx > rawRoomSize * 2 / 3)
            woodArea.emplace_back(x, y);
        }
      }
    if (!placeSettlement(game, context, team, home, {cx, cy}, "starts"))
      return false;
    teamWheatAreas.push_back(wheatArea);
    teamWoodAreas.push_back(woodArea);
  }
  context.stage = "resources";
  // Guaranteed per-team wheat/wood claim their (small) reserved area first. scatterResources'
  // noise bands skip any tile that already carries a resource, so running it after leaves these
  // reservations alone; running it first let a solid band fill a whole sliver area with a
  // different resource type before the guarantee got a turn, failing the placement outright.
  for (int team = 0; team < context.request.nbTeams; ++team)
    if (!placeResourceClumpInArea(game.map, context, teamWheatAreas[team], CORN,
                                  2) ||
        !placeResourceClumpInArea(game.map, context, teamWoodAreas[team], WOOD,
                                  2))
      return false;
  scatterResources(game, context, {o.corn, o.wood, o.stone, o.algae, o.fruit});
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      if (stoneWall[y * width + x])
        game.map.setResource(x, y, STONE, 1);
  return true;
}
} // namespace

MazeOptions::MazeOptions(const GenerationRequest &r)
    : cellSize(r.option("cell-size")), roomSize(r.option("room-size")),
      corridorWidth(r.option("corridor-width")),
      loopiness(r.option("loopiness")), corn(r.option("wheat")),
      wood(r.option("wood")), stone(r.option("stone")),
      algae(r.option("algae")), fruit(r.option("fruit")) {}

GeneratorDefinition mazeDefinition() {
  return {"maze",
          11,
          "Maze",
          4,
          false,
          {{"cell-size",
            "Cell size",
            20,
            32,
            1,
            24,
            ControlGroup::Layout,
            false,
            false,
            {20, 24, 32}},
           {"room-size", "Room size", 12, 16, 1, 16, ControlGroup::Layout},
           {"corridor-width",
            "Corridor width",
            1,
            5,
            1,
            3,
            ControlGroup::Layout,
            false,
            false,
            {1, 3, 5}},
           {"loopiness",
            "Loopiness",
            0,
            100,
            1,
            25,
            ControlGroup::Layout,
            false,
            false,
            {0, 10, 25, 50, 75, 100}},
           {"wheat", "Wheat", 0, 64, 1, 50, ControlGroup::Resources},
           {"wood", "Wood", 0, 64, 1, 50, ControlGroup::Resources},
           {"stone", "Stone", 0, 64, 1, 50, ControlGroup::Resources},
           {"algae", "Algae", 0, 64, 1, 50, ControlGroup::Resources},
           {"fruit", "Fruit", 0, 64, 1, 4, ControlGroup::Resources}},
          generate,
          true,
          validate};
}
