// SPDX-License-Identifier: GPL-3.0-or-later
#include "LatticeGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Resources.h"
#include "Settlements.h"
#include <cmath>
using namespace MapGeneration;
namespace {
constexpr double pi = 3.14159265358979323846;

bool generate(Game &game, GenerationContext &context) {
  context.stage = "lattice terrain";
  const LatticeOptions o(context.request);
  const int width = game.map.getW(), height = game.map.getH();
  game.map.makeHomogenMap(WATER);
  for (int i = 0; i < context.request.nbTeams; ++i)
    game.addTeam();

  // controlSand consumes one tile from each edge of both terrain bands. Build
  // the undermap two tiles wider so the controls describe the playable result.
  const int rawIsletSize = o.isletSize + 2;
  const int period = rawIsletSize + o.channelWidth + 2;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      game.map.setUMTerrain(
          x, y,
          (x % period < rawIsletSize && y % period < rawIsletSize) ? GRASS
                                                                   : WATER);

  std::vector<MapGeneratorPoint> homes;
  const double ringRadius = std::min(width, height) * 0.35;
  for (int i = 0; i < context.request.nbTeams; ++i) {
    const double angle = 2.0 * pi * i / context.request.nbTeams + 0.4;
    const int hx =
        game.map.normalizeX(int(width / 2 + ringRadius * std::cos(angle)));
    const int hy =
        game.map.normalizeY(int(height / 2 + ringRadius * std::sin(angle)));
    homes.emplace_back(hx, hy);
    for (int dy = -o.homeRadius; dy <= o.homeRadius; ++dy)
      for (int dx = -o.homeRadius; dx <= o.homeRadius; ++dx)
        if (dx * dx + dy * dy <= o.homeRadius * o.homeRadius)
          game.map.setUMTerrain(hx + dx, hy + dy, GRASS);
  }
  game.map.controlSand();
  game.map.rebuildTerrain();

  std::vector<std::vector<MapGeneratorPoint>> teamWheatAreas, teamWoodAreas;
  for (int team = 0; team < context.request.nbTeams; ++team) {
    std::vector<unsigned char> home(size_t(width) * height, 0);
    std::vector<MapGeneratorPoint> wheatArea, woodArea;
    for (int dy = -o.homeRadius; dy <= o.homeRadius; ++dy)
      for (int dx = -o.homeRadius; dx <= o.homeRadius; ++dx)
        if (dx * dx + dy * dy <= o.homeRadius * o.homeRadius) {
          const int x = game.map.normalizeX(homes[team].x + dx);
          const int y = game.map.normalizeY(homes[team].y + dy);
          home[y * width + x] = 1;
          if (game.map.isGrass(x, y) && dx < -o.homeRadius / 2)
            wheatArea.emplace_back(x, y);
          if (game.map.isGrass(x, y) && dx > o.homeRadius / 2)
            woodArea.emplace_back(x, y);
        }
    if (!placeSettlement(game, context, team, home, homes[team], "starts"))
      return false;
    teamWheatAreas.push_back(wheatArea);
    teamWoodAreas.push_back(woodArea);
  }
  context.stage = "resources";
  scatterResources(game, context, {o.corn, o.wood, o.stone, o.algae, o.fruit});
  for (int team = 0; team < context.request.nbTeams; ++team)
    if (!placeResourceClumpInArea(game.map, context, teamWheatAreas[team], CORN,
                                  2) ||
        !placeResourceClumpInArea(game.map, context, teamWoodAreas[team], WOOD,
                                  2))
      return false;
  return true;
}
} // namespace

LatticeOptions::LatticeOptions(const GenerationRequest &r)
    : isletSize(r.option("islet-size")),
      channelWidth(r.option("channel-width")),
      homeRadius(r.option("home-radius")), corn(r.option("wheat")),
      wood(r.option("wood")), stone(r.option("stone")),
      algae(r.option("algae")), fruit(r.option("fruit")) {}

GeneratorDefinition latticeDefinition() {
  return {
      "lattice",
      10,
      "Lattice",
      2,
      false,
      {{"islet-size", "Islet size", 1, 4, 1, 2, ControlGroup::Terrain},
       {"channel-width", "Channel width", 2, 4, 1, 3, ControlGroup::Terrain},
       {"home-radius", "Home radius", 12, 20, 2, 16, ControlGroup::Layout},
       {"wheat", "Wheat", 0, 64, 1, 50, ControlGroup::Resources},
       {"wood", "Wood", 0, 64, 1, 50, ControlGroup::Resources},
       {"stone", "Stone", 0, 64, 1, 50, ControlGroup::Resources},
       {"algae", "Algae", 0, 64, 1, 50, ControlGroup::Resources},
       {"fruit", "Fruit", 0, 64, 1, 4, ControlGroup::Resources}},
      generate};
}
