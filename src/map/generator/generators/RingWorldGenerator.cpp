// SPDX-License-Identifier: GPL-3.0-or-later
#include "RingWorldGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Resources.h"
#include "Settlements.h"
#include "Unit.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// One continental belt wraps all the way around the map along its longer axis, and the ocean on
// either side of it meets itself across the other axis's wrap. There are no corners and no back
// line: every colony has exactly two land neighbours along the belt.
//
// Everything that shapes the belt is periodic in the map's own size - whole-number harmonics for
// its centre line and width, lattice noise whose cells tile the torus for its coastline - so the
// seam can't be seen. Terrain is written straight to the undermap with an order-independent beach
// pass rather than Map::controlSand(), whose in-place raster scan makes shorelines depend on scan
// order.
namespace {

constexpr double pi = 3.14159265358979323846;

// Belt tiles along the ring each colony needs; validateRequest rejects crowding below this.
constexpr int kBeltPerColony = 24;
// A dry spine this many tiles either side of the centre line is never coast or lake, so the belt
// is always one landmass that closes on itself. The coast's budget is measured from it.
constexpr double kSpineHalf = 5.0;
// Half the narrowest ocean allowed between the belt's two coasts, measured across the wrap (or 6%
// of the map's breadth, if that is more).
constexpr double kOceanHalf = 5.0;
// Steepest the belt's centre line and half-width may change per tile along it.
constexpr double kMaxBendSlope = 0.45;
constexpr double kMaxSwingSlope = 0.3;
// Land kept between a lake and the ocean and between two lakes; water kept around an island.
constexpr int kLakeShore = 5;
constexpr int kLakeGap = 5;
constexpr int kIslandMoat = 7;
// Every swarm stands this many tiles from its nearest water, so every start has the same fertile
// shore within reach.
constexpr int kHomeShore = 6;
// Every home starts identical: fixed tile counts, wheat and wood 1:1 as for any guaranteed
// placement, outside a clear ring this wide that keeps the swarm's workers free to walk out.
constexpr int kHomeWheat = 16;
constexpr int kHomeWood = 16;
constexpr int kHomeStone = 10;
constexpr int kSwarmClearance = 2;

constexpr int kUnreached = INT_MIN;

double unitDraw(std::mt19937 &rng) { return rng() / 4294967296.0; }

// Belt coordinates: u runs along the belt and v across it. The belt follows the map's longer
// axis, so a tall map gets one that wraps top to bottom.
struct Axes {
  int width, height;
  bool alongX;
  int length() const { return alongX ? width : height; }
  int breadth() const { return alongX ? height : width; }
  int u(int x, int y) const { return alongX ? x : y; }
  int v(int x, int y) const { return alongX ? y : x; }
  int stepU(int dx, int dy) const { return alongX ? dx : dy; }
};

Axes axesFor(int width, int height) { return {width, height, width >= height}; }

int wrapSquare(int width, int height, int ax, int ay, int bx, int by) {
  int dx = std::abs(ax - bx), dy = std::abs(ay - by);
  dx = std::min(dx, width - dx);
  dy = std::min(dy, height - dy);
  return dx * dx + dy * dy;
}

// Breadth-first 8-neighbour steps on the torus from every source tile through open tiles; -1
// where the flood never arrives.
std::vector<int> stepsFrom(int width, int height, const std::vector<unsigned char> &source,
                           const std::vector<unsigned char> &open) {
  std::vector<int> dist(size_t(width) * height, -1);
  std::vector<int> queue;
  queue.reserve(dist.size());
  for (size_t i = 0; i < dist.size(); ++i)
    if (source[i]) {
      dist[i] = 0;
      queue.push_back(int(i));
    }
  for (size_t head = 0; head < queue.size(); ++head) {
    const int x = queue[head] % width, y = queue[head] / width;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const size_t n = size_t((y + dy + height) % height) * width + (x + dx + width) % width;
        if (dist[n] < 0 && open[n]) {
          dist[n] = dist[queue[head]] + 1;
          queue.push_back(int(n));
        }
      }
  }
  return dist;
}

// A smooth closed curve along the belt, one sample per tile. Its harmonics are whole numbers of
// cycles per map length, so it meets itself exactly at the seam. Normalised to [-1, 1]; `slope`
// receives its steepest step between neighbouring samples.
std::vector<double> closedCurve(int length, double falloff, std::mt19937 &rng, double &slope) {
  const int harmonics = std::clamp(length / 40, 2, 10);
  std::vector<double> amplitude(harmonics), phase(harmonics);
  for (int k = 0; k < harmonics; ++k) {
    amplitude[k] = (0.3 + 0.7 * unitDraw(rng)) / std::pow(k + 1.0, falloff);
    phase[k] = 2 * pi * unitDraw(rng);
  }
  std::vector<double> curve(length, 0.0);
  double peak = 0;
  for (int u = 0; u < length; ++u) {
    for (int k = 0; k < harmonics; ++k)
      curve[u] += amplitude[k] * std::sin(2 * pi * (k + 1) * u / length + phase[k]);
    peak = std::max(peak, std::abs(curve[u]));
  }
  slope = 0;
  if (peak <= 0)
    return curve;
  for (double &sample : curve)
    sample /= peak;
  for (int u = 0; u < length; ++u)
    slope = std::max(slope, std::abs(curve[(u + 1) % length] - curve[u]));
  return curve;
}

// Value noise whose lattice cells divide the map exactly (both sides are powers of two), so it
// tiles the torus with no seam in either direction. Normalised to [-1, 1].
std::vector<float> torusNoise(int width, int height, std::mt19937 &rng) {
  static const std::pair<int, double> octaves[] = {{32, 0.4}, {16, 0.3}, {8, 0.2}, {4, 0.1}};
  std::vector<double> field(size_t(width) * height, 0.0);
  const auto ease = [](double t) { return t * t * (3 - 2 * t); };
  for (const auto &octave : octaves) {
    const int cell = std::min({octave.first, width, height});
    const int columns = width / cell, rows = height / cell;
    std::vector<double> lattice(size_t(columns) * rows);
    for (double &value : lattice)
      value = 2 * unitDraw(rng) - 1;
    for (int y = 0; y < height; ++y) {
      const int r0 = y / cell, r1 = (r0 + 1) % rows;
      const double ty = ease(double(y % cell) / cell);
      for (int x = 0; x < width; ++x) {
        const int c0 = x / cell, c1 = (c0 + 1) % columns;
        const double tx = ease(double(x % cell) / cell);
        const double top = lattice[r0 * columns + c0] * (1 - tx) + lattice[r0 * columns + c1] * tx;
        const double bottom =
            lattice[r1 * columns + c0] * (1 - tx) + lattice[r1 * columns + c1] * tx;
        field[size_t(y) * width + x] += octave.second * (top * (1 - ty) + bottom * ty);
      }
    }
  }
  double peak = 0;
  for (double value : field)
    peak = std::max(peak, std::abs(value));
  std::vector<float> result(field.size(), 0.0f);
  if (peak > 0)
    for (size_t i = 0; i < field.size(); ++i)
      result[i] = float(field[i] / peak);
  return result;
}

struct Belt {
  Axes axes;
  std::vector<double> centre, halfWidth; // per u
  std::vector<float> coast;              // per tile, in [-1, 1]
  double roughness;                      // coast displacement, in tiles, where |coast| is 1

  // Signed distance across the belt from its centre line, wrapped into [-breadth/2, breadth/2].
  double across(int x, int y) const {
    return std::remainder(axes.v(x, y) - centre[axes.u(x, y)], double(axes.breadth()));
  }
  bool land(int x, int y) const {
    return std::abs(across(x, y)) <
           halfWidth[axes.u(x, y)] + roughness * coast[size_t(y) * axes.width + x];
  }
};

// The belt's centre line bends and its half-width swings along its length; the coast is roughened
// on top of that. Swing and roughness share one budget: the coast may bulge out until the ocean is
// at its narrowest and pinch in until it reaches the spine, never further, so the belt can neither
// break nor swallow the ocean whatever the controls say.
Belt shapeBelt(const Axes &axes, GenerationContext &context, const RingWorldOptions &options) {
  std::mt19937 &rng = context.stream("belt");
  const int length = axes.length();
  const double breadth = axes.breadth();
  const double meanHalf = options.beltWidth / 100.0 * breadth / 2;
  const double oceanHalf = std::max(kOceanHalf, 0.06 * breadth);
  const double budget =
      std::max(0.0, std::min(breadth / 2 - oceanHalf - meanHalf, meanHalf - kSpineHalf - 1));
  double swing = 0.22 * meanHalf;
  double roughness = options.coastRoughness / 100.0 * 0.9 * meanHalf;
  if (swing + roughness > budget) {
    const double shrink = budget / (swing + roughness);
    swing *= shrink;
    roughness *= shrink;
  }
  double bendSlope = 0, swingSlope = 0;
  const std::vector<double> bend = closedCurve(length, 1.6, rng, bendSlope);
  const std::vector<double> widthCurve = closedCurve(length, 1.2, rng, swingSlope);
  // A belt that doesn't wind still draws its curve, so the rest of the belt is unchanged.
  double bendAmplitude = options.windingBelt ? 0.11 * breadth : 0.0;
  if (bendSlope > 0)
    bendAmplitude = std::min(bendAmplitude, kMaxBendSlope / bendSlope);
  if (swingSlope > 0)
    swing = std::min(swing, kMaxSwingSlope / swingSlope);

  Belt belt{axes, std::vector<double>(length), std::vector<double>(length),
            torusNoise(axes.width, axes.height, rng), roughness};
  for (int u = 0; u < length; ++u) {
    belt.centre[u] = breadth / 2 + bendAmplitude * bend[u];
    belt.halfWidth[u] = meanHalf + swing * widthCurve[u];
  }
  return belt;
}

// Inland lakes for fertility. Each keeps kLakeShore tiles of land to the ocean, kLakeGap to other
// lakes, and stays clear of the spine, so no lake or chain of lakes can cut the belt. Density is
// lakes per 4096 tiles of belt.
void carveLakes(std::vector<unsigned char> &terrain, const Belt &belt, GenerationContext &context,
                int density) {
  const int width = belt.axes.width, height = belt.axes.height;
  const size_t area = size_t(width) * height;
  std::vector<unsigned char> water(area), land(area);
  std::vector<int> dry;
  for (size_t i = 0; i < area; ++i) {
    water[i] = terrain[i] == WATER;
    land[i] = !water[i];
    if (land[i])
      dry.push_back(int(i));
  }
  if (density <= 0 || dry.empty())
    return;
  const std::vector<int> shore = stepsFrom(width, height, water, land);
  const int wanted = int(std::lround(density * double(dry.size()) / 4096.0));
  const double scale = std::clamp(std::sqrt(belt.axes.breadth() / 128.0), 0.8, 1.6);
  struct Lake {
    int x, y;
    double reach;
  };
  std::vector<Lake> lakes;
  for (int attempt = 0; int(lakes.size()) < wanted && attempt < wanted * 40; ++attempt) {
    const int at = dry[context.bounded("lakes", dry.size())];
    const int x = at % width, y = at / width;
    const RadialShape shape((4 + context.bounded("lakes", 5)) * scale, 0.35, context, "lakes");
    const double reach = shape.maximumRadius();
    // The centre line leans at most kMaxBendSlope per tile, so a lake's far side can sit that much
    // nearer the spine than its centre.
    if (shore[at] < reach + kLakeShore ||
        std::abs(belt.across(x, y)) < kSpineHalf + (1 + kMaxBendSlope) * reach + 1)
      continue;
    bool clear = true;
    for (const Lake &other : lakes) {
      const double gap = reach + other.reach + kLakeGap;
      clear = clear && wrapSquare(width, height, x, y, other.x, other.y) >= gap * gap;
    }
    if (!clear)
      continue;
    const int r = int(std::ceil(reach));
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx)
        if (std::hypot(double(dx), double(dy)) < shape.radiusAt(std::atan2(double(dy), double(dx))))
          terrain[size_t((y + dy + height) % height) * width + (x + dx + width) % width] = WATER;
    lakes.push_back({x, y, reach});
  }
}

struct Island {
  int x, y;
  double reach;
  std::vector<int> tiles;
};

// Small islands out in the open ocean, each well clear of the belt and of each other so they are
// only ever reached by swimming. The control counts islands per 128x128 of map.
std::vector<Island> raiseIslands(std::vector<unsigned char> &terrain, int width, int height,
                                 GenerationContext &context, int perStandardMap) {
  std::vector<Island> islands;
  if (perStandardMap <= 0)
    return islands;
  const size_t area = size_t(width) * height;
  std::vector<unsigned char> water(area), land(area);
  std::vector<int> sea;
  for (size_t i = 0; i < area; ++i) {
    water[i] = terrain[i] == WATER;
    land[i] = !water[i];
    if (water[i])
      sea.push_back(int(i));
  }
  if (sea.empty())
    return islands;
  const std::vector<int> offshore = stepsFrom(width, height, land, water);
  const int wanted = std::max(1, int(std::lround(perStandardMap * double(area) / 16384.0)));
  // Never smaller than on a 128-tile map: any smaller and the beach leaves no grass for a prize.
  const double scale = std::clamp(std::sqrt(std::min(width, height) / 128.0), 1.0, 1.6);
  for (int attempt = 0; int(islands.size()) < wanted && attempt < wanted * 40; ++attempt) {
    const int at = sea[context.bounded("islands", sea.size())];
    const int x = at % width, y = at / width;
    const RadialShape shape((4 + context.bounded("islands", 3)) * scale, 0.3, context, "islands");
    const double reach = shape.maximumRadius();
    if (offshore[at] < reach + kIslandMoat)
      continue;
    bool clear = true;
    for (const Island &other : islands) {
      const double gap = reach + other.reach + kIslandMoat;
      clear = clear && wrapSquare(width, height, x, y, other.x, other.y) >= gap * gap;
    }
    if (!clear)
      continue;
    Island island{x, y, reach, {}};
    const int r = int(std::ceil(reach));
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx)
        if (std::hypot(double(dx), double(dy)) <
            shape.radiusAt(std::atan2(double(dy), double(dx)))) {
          const int i = (y + dy + height) % height * width + (x + dx + width) % width;
          terrain[i] = GRASS;
          island.tiles.push_back(i);
        }
    islands.push_back(std::move(island));
  }
  return islands;
}

// Grass may never touch water (Map::regenerateMap reads each tile from its four undermap corners),
// so every land corner beside water becomes sand. Unlike Map::controlSand() this reads only the
// original terrain, so the result doesn't depend on scan order and water is never eaten away.
void layBeaches(std::vector<unsigned char> &terrain, int width, int height) {
  const std::vector<unsigned char> original(terrain);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const size_t i = size_t(y) * width + x;
      if (original[i] == WATER)
        continue;
      bool shore = false;
      for (int dy = -1; dy <= 1 && !shore; ++dy)
        for (int dx = -1; dx <= 1 && !shore; ++dx)
          shore = original[size_t((y + dy + height) % height) * width + (x + dx + width) % width] ==
                  WATER;
      if (shore)
        terrain[i] = SAND;
    }
}

// A swarm anchored at (x, y) needs pure grass under its 4x4 footprint and dry, unoccupied ground in
// the ring around it where its workers will stand.
bool siteFits(const Map &map, int x, int y) {
  for (int dy = -1; dy <= 4; ++dy)
    for (int dx = -1; dx <= 4; ++dx) {
      const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
      const bool footprint = dx >= 0 && dx < 4 && dy >= 0 && dy < 4;
      if (footprint ? !map.isGrass(nx, ny) : map.isWater(nx, ny))
        return false;
      if (map.getBuilding(nx, ny) != NOGBID || map.getGroundUnit(nx, ny) != NOGUID)
        return false;
    }
  return true;
}

// Colonies are dealt evenly spaced slots around the ring from a random starting point, with a
// little jitter, and alternate between the belt's two coasts (or all take one). Within its slot a
// colony takes the site whose nearest water is closest to kHomeShore tiles away, so every start
// has the same shore.
bool placeColonies(Game &game, GenerationContext &context, const Belt &belt, bool bothCoasts) {
  Map &map = game.map;
  const Axes &axes = belt.axes;
  const int width = axes.width, height = axes.height, length = axes.length();
  const int teams = context.request.nbTeams;
  const size_t area = size_t(width) * height;
  std::vector<unsigned char> water(area), dry(area);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      water[size_t(y) * width + x] = map.isWater(x, y);
      dry[size_t(y) * width + x] = !map.isWater(x, y);
    }
  const std::vector<int> shore = stepsFrom(width, height, water, dry);
  // Only the belt itself: an island or a stretch of rough coast cut off at sea can offer the same
  // shore, but a colony there would have no land neighbours at all. The spine is always dry, so
  // the belt is whatever land is reached from it.
  std::vector<unsigned char> spine(area, 0);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      spine[size_t(y) * width + x] =
          !map.isWater(x, y) && std::abs(belt.across(x, y)) < kSpineHalf - 1;
  const std::vector<int> onBelt = stepsFrom(width, height, spine, dry);
  const double slot = double(length) / teams;
  const double first = context.bounded("colonies", 3600) / 3600.0 * length;
  const int firstSide = int(context.bounded("colonies", 2));
  std::vector<MapGeneratorPoint> placed;
  for (int team = 0; team < teams; ++team) {
    const double jitter = (int(context.bounded("colonies", 2001)) - 1000) / 1000.0 * 0.08 * slot;
    const double target = first + team * slot + jitter;
    const double side = (firstSide + (bothCoasts ? team : 0)) % 2 ? 1.0 : -1.0;
    const double spacing = 0.5 * slot;
    int bestX = -1, bestY = -1;
    double bestScore = std::numeric_limits<double>::max();
    for (double reach : {0.12, 0.25, 0.5}) {
      const double window = std::max(3.0, reach * slot);
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const double offset =
              std::abs(std::remainder(axes.u(x, y) + 1.5 - target, double(length)));
          if (offset > window || onBelt[size_t(y) * width + x] < 0 || !siteFits(map, x, y))
            continue;
          bool crowded = false;
          for (const MapGeneratorPoint &other : placed)
            crowded = crowded || wrapSquare(width, height, x, y, other.x, other.y) <
                                     spacing * spacing;
          if (crowded)
            continue;
          int gap = INT_MAX;
          for (int dy = 0; dy < 4; ++dy)
            for (int dx = 0; dx < 4; ++dx)
              gap = std::min(gap, shore[size_t(map.normalizeY(y + dy)) * width +
                                        map.normalizeX(x + dx)]);
          const double across = belt.across(map.normalizeX(x + 2), map.normalizeY(y + 2));
          const double score =
              3.0 * std::abs(gap - kHomeShore) + offset / window + (across * side < 0 ? 6.0 : 0.0);
          if (score < bestScore) {
            bestScore = score;
            bestX = x;
            bestY = y;
          }
        }
      if (bestX >= 0)
        break;
    }
    if (bestX < 0) {
      context.detail =
          "Colony " + std::to_string(team) + ": no room for a swarm on the belt near its slot";
      return false;
    }
    std::vector<unsigned char> home(area, 0);
    for (int dy = -3; dy <= 6; ++dy)
      for (int dx = -3; dx <= 6; ++dx) {
        const int nx = map.normalizeX(bestX + dx), ny = map.normalizeY(bestY + dy);
        if (!map.isWater(nx, ny))
          home[size_t(ny) * width + nx] = 1;
      }
    if (!placeSettlement(game, context, team, home, MapGeneratorPoint(bestX, bestY), "starts"))
      return false;
    placed.emplace_back(bestX, bestY);
  }
  return true;
}

// Grows a compact patch of one resource outward from a seed tile, breadth-first over the four
// cardinal neighbours, onto tiles the predicate allows. Returns how many tiles it placed.
template <typename Eligible>
int growPatch(Map &map, int seed, int type, int count, Eligible eligible) {
  const int width = map.getW(), height = map.getH();
  std::vector<unsigned char> queued(size_t(width) * height, 0);
  std::vector<int> frontier{seed};
  queued[seed] = 1;
  int placed = 0;
  static const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (size_t head = 0; head < frontier.size() && placed < count; ++head) {
    const int i = frontier[head], x = i % width, y = i / width;
    if (!eligible(i) || !map.isResourceAllowed(x, y, type))
      continue;
    map.setResource(x, y, type, 1);
    ++placed;
    for (const auto &step : steps) {
      const int n = map.normalizeY(y + step[1]) * width + map.normalizeX(x + step[0]);
      if (!queued[n]) {
        queued[n] = 1;
        frontier.push_back(n);
      }
    }
  }
  return placed;
}

// Every home's starter kit: a wheat and a wood patch on the most fertile ground a short walk from
// the swarm, and a little stone further inland.
void furnishHomes(Game &game, GenerationContext &context) {
  Map &map = game.map;
  const int width = map.getW(), height = map.getH(), teams = context.request.nbTeams;
  const size_t area = size_t(width) * height;
  std::vector<unsigned char> water(area), dry(area), reserved(area, 0);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      water[size_t(y) * width + x] = map.isWater(x, y);
      dry[size_t(y) * width + x] = !map.isWater(x, y);
    }
  for (int team = 0; team < teams; ++team)
    for (int dy = -kSwarmClearance; dy < 4 + kSwarmClearance; ++dy)
      for (int dx = -kSwarmClearance; dx < 4 + kSwarmClearance; ++dx)
        reserved[size_t(map.normalizeY(context.bootY[team] + dy)) * width +
                 map.normalizeX(context.bootX[team] + dx)] = 1;
  const std::vector<int> shore = stepsFrom(width, height, water, dry);
  const auto eligible = [&](int i) {
    const int x = i % width, y = i / width;
    return !reserved[i] && map.isGrass(x, y) && !map.isResource(x, y) &&
           map.getBuilding(x, y) == NOGBID && map.getGroundUnit(x, y) == NOGUID;
  };
  for (int team = 0; team < teams; ++team) {
    std::vector<unsigned char> footprint(area, 0);
    for (int dy = 0; dy < 4; ++dy)
      for (int dx = 0; dx < 4; ++dx)
        footprint[size_t(map.normalizeY(context.bootY[team] + dy)) * width +
                  map.normalizeX(context.bootX[team] + dx)] = 1;
    const std::vector<int> walk = stepsFrom(width, height, footprint, dry);
    // Wettest (or, for stone, driest) eligible tile in a walking band, nearest first on ties.
    const auto pick = [&](int nearest, int farthest, bool wet, int avoid) {
      int best = -1;
      for (int i = 0; i < int(area); ++i) {
        if (walk[i] < nearest || walk[i] > farthest || !eligible(i))
          continue;
        if (avoid >= 0 &&
            wrapSquare(width, height, i % width, i / width, avoid % width, avoid / width) < 36)
          continue;
        const bool better =
            best < 0 || (wet ? shore[i] < shore[best] : shore[i] > shore[best]) ||
            (shore[i] == shore[best] && walk[i] < walk[best]);
        if (better)
          best = i;
      }
      return best;
    };
    const int wheat = pick(5, 9, true, -1);
    if (wheat >= 0)
      growPatch(map, wheat, CORN, kHomeWheat, eligible);
    const int wood = pick(5, 9, true, wheat);
    if (wood >= 0)
      growPatch(map, wood, WOOD, kHomeWood, eligible);
    const int stone = pick(7, 12, false, -1);
    if (stone >= 0)
      growPatch(map, stone, STONE, kHomeStone, eligible);
  }
}

// Each island carries one themed prize, as on Fjord continent's outliers.
void stockIslands(Map &map, GenerationContext &context, const std::vector<Island> &islands) {
  const int width = map.getW();
  for (const Island &island : islands) {
    std::vector<MapGeneratorPoint> grass;
    for (int i : island.tiles)
      if (map.isGrass(i % width, i / width))
        grass.emplace_back(i % width, i / width);
    if (grass.empty())
      continue;
    MapGeneratorPoint centre(island.x, island.y);
    if (!map.isGrass(centre.x, centre.y))
      centre = grass[context.bounded("islands", grass.size())];
    switch (context.bounded("islands", 3)) {
    case 0:
      placeResourceClump(map, context, centre, STONE, 2);
      break;
    case 1:
      placeResourceClump(map, context, centre, CHERRY + int(context.bounded("islands", 3)), 2);
      break;
    default:
      placeResourceClump(map, context, centre, CORN, 2);
      break;
    }
  }
}

// Algae in the shallows along every coast, the ocean's and the lakes' alike: close enough to shore
// to regrow and to be harvested.
void seedAlgae(Map &map, GenerationContext &context, int algaePercent) {
  const int width = map.getW(), height = map.getH();
  const size_t area = size_t(width) * height;
  std::vector<unsigned char> water(area), dry(area);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      water[size_t(y) * width + x] = map.isWater(x, y);
      dry[size_t(y) * width + x] = !map.isWater(x, y);
    }
  const std::vector<int> offshore = stepsFrom(width, height, dry, water);
  std::vector<MapGeneratorPoint> shallows;
  for (int i = 0; i < int(area); ++i)
    if (offshore[i] >= 2 && offshore[i] <= 5)
      shallows.emplace_back(i % width, i / width);
  if (shallows.empty())
    return;
  for (int clump = 0; clump < scaledCount(int(shallows.size()) / 90, algaePercent); ++clump)
    placeResourceClump(map, context, shallows[context.bounded("resources", shallows.size())],
                       ALGA, 1);
}

void clearAroundSwarms(Map &map, const GenerationContext &context) {
  for (int team = 0; team < context.request.nbTeams; ++team)
    for (int dy = -kSwarmClearance; dy < 4 + kSwarmClearance; ++dy)
      for (int dx = -kSwarmClearance; dx < 4 + kSwarmClearance; ++dx) {
        const int x = map.normalizeX(context.bootX[team] + dx);
        const int y = map.normalizeY(context.bootY[team] + dy);
        if (map.isResource(x, y))
          map.setNoResource(x, y, 1);
      }
}

// Deposits may land anywhere, and stone is never cleared in play, so a band of them could wall
// the belt off. This keeps the ring open: for each colony in order around the belt, the cheapest
// walk forward to the next (resources cost one, open ground nothing; water and buildings are
// impassable) is found in the belt's unrolled cover, and only the deposits on it are cleared. The
// last walk crosses the seam back to the first colony, so together they close one loop all the way
// around the map. Almost always nothing is in the way and nothing is cleared.
bool openBeltRoad(Game &game, GenerationContext &context, const Axes &axes) {
  Map &map = game.map;
  const int width = map.getW(), height = map.getH(), length = axes.length();
  const int teams = context.request.nbTeams;
  const int area = width * height;
  constexpr int kLayers = 4; // windings -1 to 2 along the belt
  std::vector<std::vector<int>> workers(teams);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const Uint16 gid = map.getGroundUnit(x, y);
      if (gid != NOGUID && Unit::GIDtoTeam(gid) < teams)
        workers[Unit::GIDtoTeam(gid)].push_back(y * width + x);
    }
  std::vector<std::pair<double, int>> order;
  for (int team = 0; team < teams; ++team)
    order.emplace_back(
        std::fmod(axes.u(context.bootX[team], context.bootY[team]) + 2.0, double(length)), team);
  std::sort(order.begin(), order.end());

  std::vector<int> cost(size_t(kLayers) * area), parent(size_t(kLayers) * area);
  std::vector<unsigned char> done(size_t(kLayers) * area);
  std::vector<int> goal(area);
  // The winding that puts tile i nearest to a position along the unrolled belt.
  const auto windingNear = [&](int i, double position) {
    return int(std::lround((position - axes.u(i % width, i / width)) / length));
  };
  for (int k = 0; k < teams; ++k) {
    const double fromU = order[k].first;
    const double toU = order[(k + 1) % teams].first + (k + 1 == teams ? length : 0);
    const int from = order[k].second, to = order[(k + 1) % teams].second;
    std::fill(cost.begin(), cost.end(), INT_MAX);
    std::fill(parent.begin(), parent.end(), -1);
    std::fill(done.begin(), done.end(), 0);
    std::fill(goal.begin(), goal.end(), kUnreached);
    std::deque<int> queue;
    for (int i : workers[from]) {
      const int w = windingNear(i, fromU);
      if (w >= -1 && w < kLayers - 1) {
        cost[(w + 1) * area + i] = 0;
        queue.push_back((w + 1) * area + i);
      }
    }
    for (int i : workers[to])
      goal[i] = windingNear(i, toU);
    int reached = -1;
    while (!queue.empty() && reached < 0) {
      const int node = queue.front();
      queue.pop_front();
      if (done[node])
        continue;
      done[node] = 1;
      const int w = node / area - 1, i = node % area, x = i % width, y = i / width;
      if (goal[i] == w) {
        reached = node;
        break;
      }
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          if (!dx && !dy)
            continue;
          const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
          if (map.isWater(nx, ny) || map.getBuilding(nx, ny) != NOGBID)
            continue;
          const int step = axes.u(x, y) + axes.stepU(dx, dy);
          const int nw = w + (step < 0 ? -1 : step >= length ? 1 : 0);
          if (nw < -1 || nw >= kLayers - 1)
            continue;
          const int next = (nw + 1) * area + ny * width + nx;
          const int nextCost = cost[node] + (map.isResource(nx, ny) ? 1 : 0);
          if (nextCost < cost[next]) {
            cost[next] = nextCost;
            parent[next] = node;
            if (nextCost == cost[node])
              queue.push_front(next);
            else
              queue.push_back(next);
          }
        }
    }
    if (reached < 0) {
      context.detail = "Colony " + std::to_string(from) + " has no way along the belt to colony " +
                       std::to_string(to);
      return false;
    }
    for (int node = reached; node >= 0; node = parent[node]) {
      const int i = node % area;
      if (map.isResource(i % width, i / width))
        map.setNoResource(i % width, i / width, 1);
    }
  }
  return true;
}

bool generate(Game &game, GenerationContext &context) {
  context.stage = "ring layout";
  const RingWorldOptions options(context.request);
  Map &map = game.map;
  const int width = map.getW(), height = map.getH(), teams = context.request.nbTeams;
  map.makeHomogenMap(WATER);
  for (int i = 0; i < teams; ++i)
    game.addTeam();
  const Axes axes = axesFor(width, height);
  if (teams < 1 || axes.length() < kBeltPerColony * teams) {
    context.detail = "the belt is too short for this many colonies";
    return false;
  }

  context.stage = "ring terrain";
  const Belt belt = shapeBelt(axes, context, options);
  std::vector<unsigned char> terrain(size_t(width) * height, WATER);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      if (belt.land(x, y))
        terrain[size_t(y) * width + x] = GRASS;
  carveLakes(terrain, belt, context, options.lakeDensity);
  const std::vector<Island> islands =
      raiseIslands(terrain, width, height, context, options.resourceIslands);
  layBeaches(terrain, width, height);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      map.setUMTerrain(x, y, TerrainType(terrain[size_t(y) * width + x]));
  map.rebuildTerrain();

  context.stage = "ring colonies";
  if (!placeColonies(game, context, belt, options.bothCoasts))
    return false;

  context.stage = "ring resources";
  furnishHomes(game, context);
  stockIslands(map, context, islands);
  // The same ambient layer as Fjord continent: corn:wood 2:1, some stone, rare fruit. Algae is
  // seeded along the shallows below instead, since the shared band only scatters over land.
  scatterResources(game, context,
                   {/*corn=*/int(scaledCount(24, options.wheat)),
                    /*wood=*/int(scaledCount(12, options.wood)),
                    /*stone=*/int(scaledCount(10, options.stone)), /*algae=*/0,
                    /*fruit=*/int(scaledCount(3, options.fruit))});
  seedAlgae(map, context, options.algae);
  clearAroundSwarms(map, context);
  guaranteeStartingResources(game, context, 24, 32, 6);
  clearAroundSwarms(map, context);

  // The scatter above is sized by the resource amounts, and at the top of their range it can wall
  // a colony into its own clearing with nowhere to build — which the guarantee doesn't address,
  // since a colony buried in wheat has wheat at its feet. At any non-default amount, open such a
  // colony back up, then guarantee and clear again as above. At the defaults none of this runs.
  if (options.wheat != 100 || options.wood != 100 || options.stone != 100 ||
      options.algae != 100 || options.fruit != 100) {
    openCrampedStarts(game, context);
    guaranteeStartingResources(game, context, 24, 32, 6);
    clearAroundSwarms(map, context);
  }

  context.stage = "ring road";
  return openBeltRoad(game, context, axes);
}

std::string validate(const GenerationRequest &r) {
  const int length = std::max(1 << r.wDec, 1 << r.hDec);
  if (r.nbTeams < 1 || length < kBeltPerColony * r.nbTeams)
    return "The belt is too short for this many colonies; use a longer map or fewer colonies.";
  return "";
}

// Floods open tiles from the sources while counting how often each step crosses the seam along the
// belt. Reaching a tile again with a different count closes a walk around the torus, which is
// exactly what it means for the flooded region to wrap. `winding` is shared between floods of
// disjoint regions so each tile is labelled once; `reached` receives this flood's tiles.
template <typename Open>
bool floodAround(const Map &map, const Axes &axes, const std::vector<int> &sources, Open open,
                 std::vector<int> &winding, std::vector<int> &reached) {
  const int width = map.getW(), length = axes.length();
  reached.clear();
  if (sources.empty())
    return false;
  const int anchor = axes.u(sources[0] % width, sources[0] / width);
  for (int i : sources) {
    if (winding[i] != kUnreached)
      continue;
    // Each source starts at its image nearest the first, so a colony astride the seam is consistent.
    winding[i] = int(std::lround(double(anchor - axes.u(i % width, i / width)) / length));
    reached.push_back(i);
  }
  bool wraps = false;
  for (size_t head = 0; head < reached.size(); ++head) {
    const int i = reached[head], x = i % width, y = i / width, w = winding[i];
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy)
          continue;
        const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
        if (!open(nx, ny))
          continue;
        const int step = axes.u(x, y) + axes.stepU(dx, dy);
        const int nw = w + (step < 0 ? -1 : step >= length ? 1 : 0);
        const int n = ny * width + nx;
        if (winding[n] == kUnreached) {
          winding[n] = nw;
          reached.push_back(n);
        } else if (winding[n] != nw) {
          wraps = true;
        }
      }
  }
  return wraps;
}

// The ring's defining guarantees, checked on the finished world rather than trusted. Walking from
// colony 0's workers (water, buildings and every resource block the way; units don't, since they
// move) must reach every other colony and must close a loop around the map along the belt. And the
// seas on either side of the belt must be one ocean: some body of water wraps around the map
// beside it, so the belt can't have swallowed the ocean anywhere.
std::string validateWorld(const Game &game, const GenerationContext &context) {
  const Map &map = game.map;
  const int width = map.getW(), height = map.getH(), teams = context.request.nbTeams;
  const Axes axes = axesFor(width, height);
  const size_t area = size_t(width) * height;
  std::vector<std::vector<int>> workers(std::max(teams, 1));
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const Uint16 gid = map.getGroundUnit(x, y);
      if (gid != NOGUID && Unit::GIDtoTeam(gid) < teams)
        workers[Unit::GIDtoTeam(gid)].push_back(y * width + x);
    }
  if (teams < 1 || workers[0].empty())
    return "Colony 0 has no workers to walk the belt.";
  std::vector<int> winding(area, kUnreached), reached;
  const bool beltWraps = floodAround(
      map, axes, workers[0],
      [&map](int x, int y) {
        return !map.isWater(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID;
      },
      winding, reached);
  for (int team = 1; team < teams; ++team) {
    bool arrived = false;
    for (int i : workers[team])
      arrived = arrived || winding[i] != kUnreached;
    if (!arrived)
      return "Colony " + std::to_string(team) + " cannot walk to colony 0 along the belt.";
  }
  if (!beltWraps)
    return "The walkable belt does not reach all the way around the map.";

  std::fill(winding.begin(), winding.end(), kUnreached);
  bool oceanWraps = false;
  for (int i = 0; i < int(area) && !oceanWraps; ++i)
    if (winding[i] == kUnreached && map.isWater(i % width, i / width))
      oceanWraps = floodAround(
          map, axes, {i}, [&map](int x, int y) { return map.isWater(x, y); }, winding, reached);
  if (!oceanWraps)
    return "The seas beside the belt do not meet around the map.";
  return "";
}
} // namespace

RingWorldOptions::RingWorldOptions(const GenerationRequest &r)
    : beltWidth(r.option("belt-width")), coastRoughness(r.option("coast-roughness")),
      lakeDensity(r.option("lake-density")), resourceIslands(r.option("resource-islands")),
      windingBelt(r.option("winding-belt") != 0), bothCoasts(r.option("both-coasts") != 0),
      wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
      stone(r.option("stone-amount")), algae(r.option("algae-amount")),
      fruit(r.option("fruit-amount")) {}

GeneratorDefinition ringWorldDefinition() {
  return {"ring-world",
          16,
          "Ring world",
          1,
          false,
          // Belt width is the share of the map's breadth the belt covers on average; lake density
          // is lakes per 4096 tiles of belt; resource islands are counted per 128x128 of map.
          {{"belt-width", "Belt width", 30, 70, 5, 45, ControlGroup::Terrain},
           {"coast-roughness", "Coast roughness", 0, 100, 5, 50, ControlGroup::Terrain},
           {"lake-density", "Lake density", 0, 8, 1, 2, ControlGroup::Terrain},
           {"resource-islands", "Resource islands", 0, 10, 1, 2, ControlGroup::Resources},
           // Off, the belt's centre line runs straight round the map.
           GeneratorControl::toggle("winding-belt", "Winding belt", true, ControlGroup::Terrain),
           // Off, every colony starts on the same coast of the belt.
           GeneratorControl::toggle("both-coasts", "Colonies on both coasts", true,
                                    ControlGroup::Layout),
           // The ambient scatter's wheat, wood, stone and fruit and the shallows' algae. Every
           // home's starter patches and each island's prize stay as they are.
           GeneratorControl::percentage("wheat-amount", "Wheat amount"),
           GeneratorControl::percentage("wood-amount", "Wood amount"),
           GeneratorControl::percentage("stone-amount", "Stone amount"),
           GeneratorControl::percentage("algae-amount", "Algae amount"),
           GeneratorControl::percentage("fruit-amount", "Fruit amount")},
          generate,
          true,
          validate,
          validateWorld};
}
