// SPDX-License-Identifier: GPL-3.0-or-later
#include "FjordContinentGenerator.h"
#include "Distances.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "GlobalContainer.h"
#include "Regions.h"
#include "Resources.h"
#include "Settlements.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace MapGeneration;
namespace {
constexpr double pi = 3.14159265358979323846;
double randomAngle(GenerationContext &context) {
  return context.bounded("layout", 3600) / 3600.0 * 2 * pi;
}
void createJaggedIsland(Map &map, GenerationContext &context,
                        std::vector<int> &grid, int area, int x, int y,
                        int radius, double roughness) {
  RadialShape shape(radius, roughness, context, "outliers", 1.4);
  stampShape(grid, map.getW(), map.getH(), area,
             ShapeTransform({double(x), double(y)}, 0), shape, true);
}

bool placeBankClump(Map &map, GenerationContext &context,
                    const std::vector<MapGeneratorPoint> &centerline,
                    double progress, int side, int resourceType) {
  if (centerline.size() < 3)
    return false;
  const int target = int(progress * (centerline.size() - 1));
  for (int offset = 0; offset < int(centerline.size()); ++offset) {
    const int signedOffset = offset % 2 ? -(offset + 1) / 2 : offset / 2;
    const int i = std::max(
        1, std::min(int(centerline.size()) - 2, target + signedOffset));
    int tx = centerline[i + 1].x - centerline[i - 1].x;
    int ty = centerline[i + 1].y - centerline[i - 1].y;
    if (tx > map.getW() / 2)
      tx -= map.getW();
    else if (tx < -map.getW() / 2)
      tx += map.getW();
    if (ty > map.getH() / 2)
      ty -= map.getH();
    else if (ty < -map.getH() / 2)
      ty += map.getH();
    const double length = std::sqrt(double(tx * tx + ty * ty));
    if (length < 0.5)
      continue;
    const double nx = -ty / length * side, ny = tx / length * side;
    for (int distance = 2; distance <= 16; ++distance) {
      MapGeneratorPoint anchor(
          map.normalizeX(centerline[i].x + int(std::lround(nx * distance))),
          map.normalizeY(centerline[i].y + int(std::lround(ny * distance))));
      if (map.isResourceAllowed(anchor.x, anchor.y, resourceType) &&
          placeResourceClump(map, context, anchor, resourceType, 2) >= 3)
        return true;
    }
  }
  return false;
}
static bool generate(Game &game, GenerationContext &context) {
  context.stage = "continent";
  const FjordContinentOptions options(context.request);
  game.map.makeHomogenMap(WATER);
  for (int i = 0; i < context.request.nbTeams; ++i)
    game.addTeam();

  const int W = game.map.getW();
  const int H = game.map.getH();
  const int nbTeams = context.request.nbTeams;
  if (nbTeams < 1)
    return false;

  const double rotation = randomAngle(context) * 0.5;
  const double elongation =
      1.0 + context.bounded("layout", 1000) / 1000.0 * 0.3;
  ShapeTransform xf({W / 2.0, H / 2.0}, rotation, elongation);

  const double baseR = options.continentSize / 100.0 * std::min(W, H);
  RadialShape coast(baseR, options.roughness / 100.0, context, "coast", 1.4);
  const double coreR = baseR * 0.19;
  const double maxStretch = std::max(elongation, 1.0 / elongation);

  // One angular sector per team, evenly spaced with a little jitter so it
  // doesn't look mechanical. Deciding sectors up front (rather than deriving
  // neighbor order from wherever seed points happen to land) is what guarantees
  // every team gets pushed out to its own tip instead of sometimes landing near
  // the shared middle.
  std::vector<double> teamTheta(nbTeams);
  {
    double baseRotation = randomAngle(context);
    double slot = 2 * pi / nbTeams;
    for (int i = 0; i < nbTeams; ++i) {
      double jitter =
          ((context.bounded("layout", 2001)) / 1000.0 - 1.0) * 0.2 * slot;
      teamTheta[i] = baseRotation + i * slot + jitter;
    }
  }

  // grid holds area numbers for resource zoning, kept alongside the real
  // terrain (which is written straight onto game.map as we go, the same way
  // computeIsles and computeContestedCommons do it).
  std::vector<int> grid(W * H, 0);
  int areaNumber = 1;

  // 1) Stamp the continent.
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const auto shaped = xf.toShape({double(x), double(y)});
      double u = shaped.x, v = shaped.y;
      double theta = atan2(v, u);
      double r = sqrt(u * u + v * v);
      if (r < coast.radiusAt(theta))
        game.map.setUMatPos(x, y, GRASS, 1);
    }
  }

  // 2) Carve a fjord between every pair of angularly-neighboring teams: a
  // smooth S-curve from just outside the coast at the sector boundary, in to a
  // fixed inner radius (coreR) that every fjord stops short of -- so the disc
  // r<coreR is never touched and always stays connected land, the "palm" every
  // peninsula "finger" hangs off. Lateral wiggle is enveloped to zero at both
  // ends so the mouth stays aligned with the sector boundary and the tip stays
  // on the radial line.
  std::vector<std::vector<MapGeneratorPoint>> fjordCenterlines(nbTeams);
  if (nbTeams >= 2) {
    for (int k = 0; k < nbTeams; ++k) {
      int i = k;
      int j = (k + 1) % nbTeams;
      double ti = teamTheta[i];
      double tj = teamTheta[j];
      double d = fmod(tj - ti, 2 * pi);
      if (d < 0)
        d += 2 * pi;
      double midTheta = ti + d / 2.0;

      double mouthR = coast.radiusAt(midTheta) + 3.0;
      double mouthU = mouthR * cos(midTheta), mouthV = mouthR * sin(midTheta);
      double tipU = coreR * cos(midTheta), tipV = coreR * sin(midTheta);
      double perpU = -sin(midTheta), perpV = cos(midTheta);

      double gap = std::min(d, 2 * pi - d);
      double amplitude = gap * mouthR * 0.17;
      double phase = randomAngle(context);
      double mouthWidth =
          options.fjordWidth + 0.6 + context.bounded("layout", 1400) / 1000.0;
      double tipWidth = 0.65;

      int steps = std::max(24, (int)(mouthR - coreR));
      for (int s = 0; s <= steps; ++s) {
        double t = double(s) / steps;
        double baseU = mouthU + t * (tipU - mouthU);
        double baseV = mouthV + t * (tipV - mouthV);
        double envelope = sin(pi * t);
        double lateral = amplitude * sin(2 * pi * t + phase) * envelope;
        double u = baseU + lateral * perpU;
        double v = baseV + lateral * perpV;
        double width = mouthWidth * (1 - t) + tipWidth * t;

        const auto mapped = xf.toMap({u, v});
        double mx = mapped.x, my = mapped.y;
        int rad = (int)ceil(width * maxStretch) + 1;
        int ix = (int)lround(mx), iy = (int)lround(my);
        fjordCenterlines[k].push_back(MapGeneratorPoint(
            game.map.normalizeX(ix), game.map.normalizeY(iy)));
        for (int dy = -rad; dy <= rad; ++dy) {
          for (int dx = -rad; dx <= rad; ++dx) {
            int nx = game.map.normalizeX(ix + dx);
            int ny = game.map.normalizeY(iy + dy);
            const auto shaped = xf.toShape({double(nx), double(ny)});
            double u2 = shaped.x, v2 = shaped.y;
            double dd = (u2 - u) * (u2 - u) + (v2 - v) * (v2 - v);
            if (dd <= width * width) {
              game.map.setUMatPos(nx, ny, WATER, 1);
            }
          }
        }
      }
    }
  }

  // 3) A couple of small, unconnected resource islands out in the open sea --
  // purely a bonus for whoever explores, never touching the mainland or each
  // other.
  {
    double mainlandReach = coast.maximumRadius() * maxStretch;
    double halfMapMargin = std::min(W, H) / 2.0 - 6.0;
    int outlierCount = std::min(4, options.resourceIslands +
                                       int(context.bounded("layout", 2)));
    std::vector<MapGeneratorPoint> outlierCenters;
    std::vector<int> outlierRadii;
    for (int oi = 0; oi < outlierCount && mainlandReach + 6.0 < halfMapMargin;
         ++oi) {
      int islandRadius = 5 + context.bounded("layout", 4);
      bool placed = false;
      for (int attempt = 0; attempt < 40 && !placed; ++attempt) {
        double theta = randomAngle(context);
        double lo = mainlandReach + 6.0;
        double hi = halfMapMargin;
        double r = lo + (context.bounded("layout", 1000)) / 1000.0 * (hi - lo);
        int cx = game.map.normalizeX((int)lround(W / 2.0 + r * cos(theta)));
        int cy = game.map.normalizeY((int)lround(H / 2.0 + r * sin(theta)));

        bool clear = true;
        for (unsigned int oc = 0; oc < outlierCenters.size() && clear; ++oc) {
          int minSep = islandRadius + outlierRadii[oc] + 8;
          if (game.map.warpDistSquare(cx, cy, outlierCenters[oc].x,
                                      outlierCenters[oc].y) < minSep * minSep)
            clear = false;
        }
        if (!clear)
          continue;

        int outlierArea = areaNumber++;
        createJaggedIsland(game.map, context, grid, outlierArea, cx, cy,
                           islandRadius, 0.3);
        std::vector<MapGeneratorPoint> islandPts;
        getAllPoints(game.map, grid, outlierArea, islandPts);
        if (islandPts.empty())
          continue;
        for (unsigned int p = 0; p < islandPts.size(); ++p)
          game.map.setUMatPos(islandPts[p].x, islandPts[p].y, GRASS, 1);

        // Each island leans on one resource theme, so finding one feels like a
        // distinct little prize rather than an interchangeable resource dump.
        switch (context.bounded("layout", 3)) {
        case 0:
          placeResourceClump(
              game.map, context,
              islandPts[context.bounded("resources", islandPts.size())], STONE,
              2);
          break;
        case 1:
          placeResourceClump(
              game.map, context,
              islandPts[context.bounded("resources", islandPts.size())],
              CHERRY + context.bounded("resources", 3), 2);
          break;
        default:
          placeResourceClump(
              game.map, context,
              islandPts[context.bounded("resources", islandPts.size())], CORN,
              3);
          break;
        }

        outlierCenters.push_back(MapGeneratorPoint(cx, cy));
        outlierRadii.push_back(islandRadius);
        placed = true;
      }
    }
  }

  game.map.controlSand();

  // 3.5) Every resource so far is a deliberate, counted placement tied to a specific
  // purpose: a home starter kit, a fjord bank, the core, an outlier island. That leaves
  // the whole continent interior in between them bare grass, which reads as empty rather
  // than as a place with its own history the way a noise-painted map does. A light
  // map-wide scatter -- the same mechanism Lattice and Maze already rely on for this --
  // fills that gap with ordinary, unclaimed deposits before any of the guaranteed
  // placements below claim their own spots, so it can never compete with or bury a
  // guarantee (everything from here on is placed after, and setResource simply
  // overwrites whatever an earlier scatter happened to put on that exact tile). Algae is
  // left at zero here: the shoreline band in step 7 already places it with a shape tuned
  // to the coastline, and scattering more over open water would just fight that.
  scatterResources(game, context, {/*corn=*/18, /*wood=*/18, /*stone=*/10, /*algae=*/0, /*fruit=*/3});

  // 4) Anchor each team out at its own tip: start just inland of the coast at
  // the team's angle and only back off toward the core if that exact spot turns
  // out to be water (a sharp jaggedness dip, or a fjord belly that swung wider
  // than expected) -- the same "compute where we want to be, then confirm the
  // grid agrees" shape as chooseFreeForBuildingSquares.
  std::vector<MapGeneratorPoint> teamPts;
  teamPts.reserve(nbTeams);
  for (int i = 0; i < nbTeams; ++i) {
    double theta = teamTheta[i];
    double maxR = coast.radiusAt(theta);
    double r = std::max(coreR + 6.0, maxR - 8.0);
    double stepIn = (maxR - (coreR + 2.0)) / 30.0;
    if (stepIn <= 0.0)
      stepIn = 1.0;

    int fx = -1, fy = -1;
    for (int tries = 0; tries < 30; ++tries) {
      const auto mapped = xf.toMap({r * cos(theta), r * sin(theta)});
      double mx = mapped.x, my = mapped.y;
      int ix = game.map.normalizeX((int)lround(mx));
      int iy = game.map.normalizeY((int)lround(my));
      if (!game.map.isWater(ix, iy)) {
        fx = ix;
        fy = iy;
        break;
      }
      r -= stepIn;
    }
    if (fx < 0) {
      const auto mapped =
          xf.toMap({(coreR + 6.0) * cos(theta), (coreR + 6.0) * sin(theta)});
      double mx = mapped.x, my = mapped.y;
      fx = game.map.normalizeX((int)lround(mx));
      fy = game.map.normalizeY((int)lround(my));
    }
    teamPts.push_back(MapGeneratorPoint(fx, fy));
  }

  // 5) Connectivity: verify, don't assume. The untouched core should make this
  // unreachable in practice, but every other generator checks its own
  // invariants explicitly instead of trusting the construction, so this does
  // too.
  {
    std::vector<bool> visited(W * H, false);
    std::vector<MapGeneratorPoint> stack;
    stack.push_back(teamPts[0]);
    visited[teamPts[0].y * W + teamPts[0].x] = true;
    while (!stack.empty()) {
      MapGeneratorPoint p = stack.back();
      stack.pop_back();
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0)
            continue;
          int nx = game.map.normalizeX(p.x + dx);
          int ny = game.map.normalizeY(p.y + dy);
          if (!visited[ny * W + nx] && !game.map.isWater(nx, ny)) {
            visited[ny * W + nx] = true;
            stack.push_back(MapGeneratorPoint(nx, ny));
          }
        }
      }
    }
    for (int i = 0; i < nbTeams; ++i)
      if (!visited[teamPts[i].y * W + teamPts[i].x])
        return false;
  }

  // 6) Stone and fruit in the untouched core -- the reward for pushing to the
  // middle of the map instead of staying home.
  {
    int coreArea = areaNumber++;
    std::vector<MapGeneratorPoint> corePts;
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        if (game.map.isWater(x, y) || grid[y * W + x] != 0)
          continue;
        const auto shaped = xf.toShape({double(x), double(y)});
        double u = shaped.x, v = shaped.y;
        if (u * u + v * v <= (coreR + 4.0) * (coreR + 4.0)) {
          grid[y * W + x] = coreArea;
          corePts.push_back(MapGeneratorPoint(x, y));
        }
      }
    }
    if (!corePts.empty()) {
      placeResourceClump(game.map, context,
                         corePts[context.bounded("resources", corePts.size())],
                         STONE, 3);
      placeResourceClump(game.map, context,
                         corePts[context.bounded("resources", corePts.size())],
                         CHERRY + context.bounded("resources", 3), 2);
    }
  }

  // 7) Algae out in the open sea: any water tile clearly beyond the coastline
  // (not a fjord, not the moat-ish water right against the shore) gets an
  // occasional patch.
  std::vector<MapGeneratorPoint> algaeWater;
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      if (!game.map.isWater(x, y))
        continue;
      const auto shaped = xf.toShape({double(x), double(y)});
      double u = shaped.x, v = shaped.y;
      double theta = atan2(v, u);
      double r = sqrt(u * u + v * v);
      double shoreR = coast.radiusAt(theta);
      if (r > shoreR + 2.0 && r < shoreR + 14.0)
        algaeWater.emplace_back(x, y);
    }
  }
  if (!algaeWater.empty())
    for (int i = 0; i < std::max(1, int(algaeWater.size()) / 180); ++i)
      placeResourceClump(
          game.map, context,
          algaeWater[context.bounded("resources", algaeWater.size())], ALGA, 2);

  // 8) A light per-team starter kit so nobody is stuck waiting to reach the
  // fjord banks before they can build anything; the banks and the core are the
  // map's real economy.
  std::vector<MapGeneratorPoint> allWater;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      if (game.map.isWater(x, y))
        allWater.push_back(MapGeneratorPoint(x, y));

  for (int i = 0; i < nbTeams; ++i) {
    std::vector<MapGeneratorPoint> sources;
    sources.push_back(teamPts[i]);
    std::vector<int> distances;
    computeDistances(game.map, sources, allWater, distances);

    std::vector<MapGeneratorPoint> homePoints;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x)
        if (distances[y * W + x] >= 1 && distances[y * W + x] <= 10)
          homePoints.push_back(MapGeneratorPoint(x, y));
    if (homePoints.empty())
      return false;

    placeResourceClump(
        game.map, context,
        homePoints[context.bounded("resources", homePoints.size())], CORN, 2);
    placeResourceClump(
        game.map, context,
        homePoints[context.bounded("resources", homePoints.size())], WOOD, 2);

    std::vector<MapGeneratorPoint> stonePts = homePoints;
    chooseRandomPoints(game.map, context, stonePts, 1);
    for (unsigned int j = 0; j < stonePts.size(); ++j)
      game.map.setResource(stonePts[j].x, stonePts[j].y, STONE, 1);

    std::vector<unsigned char> home(size_t(W) * H, 0);
    for (const auto &point : homePoints)
      home[point.y * W + point.x] = 1;
    if (!placeSettlement(game, context, i, home, teamPts[i], "starts"))
      return false;
  }

  // 9) Place bank resources last so settlement and regional deposits cannot
  // overwrite the guarantee. Every side of every fjord receives both resources
  // at distinct points along its length.
  context.stage = "fjord bank resources";
  if (nbTeams >= 2)
    for (int k = 0; k < nbTeams; ++k) {
      for (int side : {-1, 1})
        if (!placeBankClump(game.map, context, fjordCenterlines[k], 0.35, side,
                            CORN) ||
            !placeBankClump(game.map, context, fjordCenterlines[k], 0.68, side,
                            WOOD))
          return false;
    }

  return true;
}

} // namespace

FjordContinentOptions::FjordContinentOptions(const GenerationRequest &r)
    : continentSize(r.option("continent-size")),
      roughness(r.option("coast-roughness")),
      fjordWidth(r.option("fjord-width")),
      resourceIslands(r.option("resource-islands")) {}

GeneratorDefinition fjordContinentDefinition() {
  return {"fjord-continent",
          12,
          "Fjord continent",
          2,
          false,
          {{"continent-size", "Continent size", 28, 40, 2, 34,
            ControlGroup::Terrain},
           {"coast-roughness", "Coast roughness", 10, 35, 1, 22,
            ControlGroup::Terrain},
           {"fjord-width", "Fjord width", 2, 6, 1, 3, ControlGroup::Terrain},
           {"resource-islands", "Resource islands", 0, 4, 1, 2,
            ControlGroup::Resources}},
          generate};
}
