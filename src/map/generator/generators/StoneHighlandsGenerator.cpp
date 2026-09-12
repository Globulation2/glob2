// SPDX-License-Identifier: GPL-3.0-or-later
#include "StoneHighlandsGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Resources.h"
#include "Settlements.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <queue>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Stone highlands: open grass valleys divided by thin ridgelines of stone, joined by narrow
// passes, with a pond in every basin.
//
// The ridgelines are the zero contour of a domain-warped cellular noise field: scattered basin
// sites, each tile labelled with its nearest site after the warp, and a stone tile wherever a
// tile touches a lower label. Stone is eternal and can't be cleared, so ridges never erode.
// Valleys are the walkable components that remain; a random spanning tree of passes joins them
// all, and loopiness opens extra passes for routes to flank along. Colonies take the roomiest
// valleys, one each while they last, and ponds fill the basins around them. The whole design is a
// pure function of the request, so validateWorld rebuilds it and checks the finished world.
namespace {

// Enclosed pockets smaller than this become part of the ridge: too small to live in, and a pass
// into one would only lead to a dead end a few tiles deep.
constexpr int kPocketTiles = 40;
// A pass needs at least this many tiles of ridge shared by exactly its two valleys, and pass
// centres keep this far apart so two passes never merge into one wide gap.
constexpr int kMinimumBoundary = 3;
constexpr int kPassSpacing = 10;
// Ponds keep this far from any ridge or pass (falling back to the minimum in cramped valleys), so
// the ground along every ridgeline stays walkable and no pond can close a valley or a pass off.
// Below the minimum, a pond's sand would reach the corners of ridge tiles, which need grass.
constexpr int kPondClearance = 6;
constexpr int kPondMinimumClearance = 4;
// Valleys smaller than this get no pond; bigger ones get another pond per kPondArea tiles.
constexpr int kPondMinimumValley = 120;
constexpr int kPondArea = 1600;
constexpr int kPondSeparation = 16;
constexpr int kMinimumPond = 12;
// Farmland, fruit and starter kits keep this far from ridges and passes: the band along every
// ridgeline is the valley's ring road, which is what keeps every pass reachable.
constexpr int kRidgeRoad = 3;
// A swarm centre keeps at least kHomeRidge (or half its valley's depth) from any ridge, sits up
// to kHomeOffset tiles out from its valley's deepest point, and keeps ponds kHomePondGap away;
// its valley's first pond is centred about kHomePondReach from it, towards the interior.
constexpr int kHomeRidge = 5;
constexpr int kHomeOffset = 10;
constexpr int kHomePondGap = 8;
constexpr int kHomePondReach = 11;
// Colonies sharing a valley, once every roomy valley has one, keep this far apart.
constexpr int kHomeSeparation = 16;
// Every colony starts with the same wheat and wood, 1:1, a few steps from the swarm: each patch
// takes the kHomeKit free tiles of the home valley nearest its anchor.
constexpr int kHomeKit = 20;
constexpr int kKitReach = 7;
constexpr int kKitSearch = 16;
// Ambient farmland stays this far from a swarm centre, so no colony is ringed in by it.
constexpr int kHomeClear = 9;
constexpr int kFarmlandPercent = 110; // ambient farmland tiles per 100 pond tiles
constexpr int kAlgaePercent = 12;     // of each pond's open water
constexpr int kFruitGrove = 6;
constexpr int kAreaPerColony = 1024;
constexpr int kFar = INT_MAX / 4;

struct Torus {
  int w, h;
  int x(int v) const { return ((v % w) + w) % w; }
  int y(int v) const { return ((v % h) + h) % h; }
  int at(int px, int py) const { return y(py) * w + x(px); }
  // Signed shortest offset from a to b.
  int offsetX(int a, int b) const {
    const int d = x(b - a);
    return d > w / 2 ? d - w : d;
  }
  int offsetY(int a, int b) const {
    const int d = y(b - a);
    return d > h / 2 ? d - h : d;
  }
  int dist2(int ax, int ay, int bx, int by) const {
    const int dx = offsetX(ax, bx), dy = offsetY(ay, by);
    return dx * dx + dy * dy;
  }
  int chebyshev(int ax, int ay, int bx, int by) const {
    return std::max(std::abs(offsetX(ax, bx)), std::abs(offsetY(ay, by)));
  }
};

template <typename T>
void shuffle(std::vector<T> &values, GenerationContext &context, const char *stream) {
  for (size_t i = values.size(); i > 1; --i)
    std::swap(values[i - 1], values[context.bounded(stream, std::uint32_t(i))]);
}

// Smooth value noise that tiles the map's torus exactly: a lattice of random values about
// `period` tiles apart (a whole number of lattice cells spans the map), blended with a
// smoothstep. Integer arithmetic throughout. Values are 0..65535.
std::vector<int> periodicNoise(int w, int h, int period, std::mt19937 &rng) {
  const int gw = std::max(1, (w + period / 2) / std::max(1, period));
  const int gh = std::max(1, (h + period / 2) / std::max(1, period));
  std::vector<int> lattice(size_t(gw) * gh);
  for (int &v : lattice)
    v = int(rng() >> 16);
  const auto smooth = [](int t) {
    return int(std::int64_t(t) * t * (3 * 1024 - 2 * t) / (1024 * 1024));
  };
  std::vector<int> field(size_t(w) * h);
  for (int y = 0; y < h; ++y) {
    const std::int64_t fy = std::int64_t(y) * gh * 1024 / h;
    const int y0 = int(fy >> 10), y1 = (y0 + 1) % gh, ty = smooth(int(fy & 1023));
    for (int x = 0; x < w; ++x) {
      const std::int64_t fx = std::int64_t(x) * gw * 1024 / w;
      const int x0 = int(fx >> 10), x1 = (x0 + 1) % gw, tx = smooth(int(fx & 1023));
      const int a = lattice[size_t(y0) * gw + x0], b = lattice[size_t(y0) * gw + x1];
      const int c = lattice[size_t(y1) * gw + x0], d = lattice[size_t(y1) * gw + x1];
      const int top = a + (b - a) * tx / 1024, bottom = c + (d - c) * tx / 1024;
      field[size_t(y) * w + x] = top + (bottom - top) * ty / 1024;
    }
  }
  return field;
}

// Octaves at period, period/2, ... weighted 2:1 per step.
std::vector<int> fractalNoise(int w, int h, int period, int octaves, std::mt19937 &rng) {
  std::vector<int> sum(size_t(w) * h, 0);
  int total = 0;
  for (int octave = 0; octave < octaves; ++octave) {
    const int weight = 1 << (octaves - 1 - octave);
    const std::vector<int> layer = periodicNoise(w, h, std::max(2, period >> octave), rng);
    for (size_t i = 0; i < sum.size(); ++i)
      sum[i] += layer[i] * weight;
    total += weight;
  }
  for (int &v : sum)
    v /= total;
  return sum;
}

// The value below which `percent` of the given samples fall.
int percentile(std::vector<int> samples, int percent) {
  if (samples.empty())
    return 0;
  const size_t k = std::min(samples.size() - 1, samples.size() * size_t(percent) / 100);
  std::nth_element(samples.begin(), samples.begin() + k, samples.end());
  return samples[k];
}

// Chebyshev steps from every tile to the nearest source tile; kFar with no sources.
std::vector<int> chebyshevDistance(const Torus &t, const std::vector<unsigned char> &source) {
  std::vector<int> distance(source.size(), kFar);
  std::vector<int> queue;
  queue.reserve(source.size());
  for (size_t i = 0; i < source.size(); ++i)
    if (source[i]) {
      distance[i] = 0;
      queue.push_back(int(i));
    }
  for (size_t head = 0; head < queue.size(); ++head) {
    const int p = queue[head], px = p % t.w, py = p / t.w;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int q = t.at(px + dx, py + dy);
        if (distance[q] == kFar) {
          distance[q] = distance[p] + 1;
          queue.push_back(q);
        }
      }
  }
  return distance;
}

struct Site {
  int x, y;
};

// Bucket rows or columns to search around bucket c: all of them when a window would wrap onto
// itself.
std::vector<int> bucketWindow(int c, int count, int radius) {
  std::vector<int> result;
  if (count <= 2 * radius + 1) {
    for (int i = 0; i < count; ++i)
      result.push_back(i);
  } else {
    for (int d = -radius; d <= radius; ++d)
      result.push_back(((c + d) % count + count) % count);
  }
  return result;
}

// Basin sites by dart throwing on the torus. Sites keep 0.83 of the requested spacing apart,
// which, once the map is saturated, leaves one site per spacing-squared tiles on average.
std::vector<Site> scatterSites(const Torus &t, int spacing, GenerationContext &context) {
  const int minimum = std::max(4, spacing * 83 / 100);
  const std::int64_t expected =
      std::max<std::int64_t>(2, std::int64_t(t.w) * t.h / (std::int64_t(spacing) * spacing));
  const int gx = std::max(1, t.w / minimum), gy = std::max(1, t.h / minimum);
  std::vector<std::vector<int>> columns(gx), rows(gy);
  for (int c = 0; c < gx; ++c)
    columns[c] = bucketWindow(c, gx, 1);
  for (int r = 0; r < gy; ++r)
    rows[r] = bucketWindow(r, gy, 1);
  std::vector<std::vector<int>> buckets(size_t(gx) * gy);
  std::vector<Site> sites;
  for (std::int64_t attempt = 0; attempt < expected * 60; ++attempt) {
    const int x = int(context.bounded("highlands-sites", t.w));
    const int y = int(context.bounded("highlands-sites", t.h));
    const int bx = x * gx / t.w, by = y * gy / t.h;
    bool clear = true;
    for (int row : rows[by]) {
      for (int column : columns[bx]) {
        for (int s : buckets[size_t(row) * gx + column])
          if (t.dist2(x, y, sites[s].x, sites[s].y) < minimum * minimum) {
            clear = false;
            break;
          }
        if (!clear)
          break;
      }
      if (!clear)
        break;
    }
    if (!clear)
      continue;
    buckets[size_t(by) * gx + bx].push_back(int(sites.size()));
    sites.push_back({x, y});
  }
  return sites;
}

// Every tile's nearest site, measured from a warped copy of the tile so cell borders wander
// instead of running straight. Positions are in sixteenths of a tile.
std::vector<int> labelCells(const Torus &t, const std::vector<Site> &sites, int spacing,
                            GenerationContext &context) {
  const int period = spacing * 3 / 2;
  const std::vector<int> warpX = fractalNoise(t.w, t.h, period, 3, context.stream("highlands-warp"));
  const std::vector<int> warpY = fractalNoise(t.w, t.h, period, 3, context.stream("highlands-warp"));
  const std::int64_t amplitude = std::int64_t(spacing) * 16 * 30 / 100;
  const int W = t.w * 16, H = t.h * 16;
  const int gx = std::max(1, t.w / spacing), gy = std::max(1, t.h / spacing);
  std::vector<std::vector<int>> buckets(size_t(gx) * gy), columns(gx), rows(gy);
  for (size_t s = 0; s < sites.size(); ++s)
    buckets[size_t(sites[s].y * gy / t.h) * gx + sites[s].x * gx / t.w].push_back(int(s));
  for (int c = 0; c < gx; ++c)
    columns[c] = bucketWindow(c, gx, 2);
  for (int r = 0; r < gy; ++r)
    rows[r] = bucketWindow(r, gy, 2);
  std::vector<int> label(size_t(t.w) * t.h, 0);
  for (int y = 0; y < t.h; ++y)
    for (int x = 0; x < t.w; ++x) {
      const size_t i = size_t(y) * t.w + x;
      int px = x * 16 + 8 + int((warpX[i] - 32768) * amplitude / 32768);
      int py = y * 16 + 8 + int((warpY[i] - 32768) * amplitude / 32768);
      px = ((px % W) + W) % W;
      py = ((py % H) + H) % H;
      const int bx = int(std::int64_t(px) * gx / W), by = int(std::int64_t(py) * gy / H);
      std::int64_t best = INT64_MAX;
      int nearest = 0;
      for (int row : rows[by])
        for (int column : columns[bx])
          for (int s : buckets[size_t(row) * gx + column]) {
            int dx = std::abs(px - (sites[s].x * 16 + 8)), dy = std::abs(py - (sites[s].y * 16 + 8));
            dx = std::min(dx, W - dx);
            dy = std::min(dy, H - dy);
            const std::int64_t d = std::int64_t(dx) * dx + std::int64_t(dy) * dy;
            if (d < best || (d == best && s < nearest)) {
              best = d;
              nearest = s;
            }
          }
      label[i] = nearest;
    }
  return label;
}

struct Pass {
  int x, y;   // centre tile
  int lo, hi; // the cleared box is [x + lo, x + hi] by [y + lo, y + hi]
  int a, b;   // the valleys it joins
};

struct Home {
  int valley, x, y; // swarm centre
};

// The terrain design: a pure function of the request, rebuilt by validateWorld.
struct Layout {
  Torus t{1, 1};
  std::vector<unsigned char> ridge;    // designed stone
  std::vector<int> valley;             // valley id; -1 on ridge tiles
  int valleys = 0;
  std::vector<Pass> passes;
  std::vector<unsigned char> passTile; // inside a pass's cleared box
  std::vector<int> ridgeDistance;      // Chebyshev steps to the nearest ridge or pass tile
  std::vector<std::vector<int>> tilesOf; // each valley's tiles outside pass boxes
  std::vector<int> pole;               // each valley's tile deepest inside it
  std::vector<Home> homes;
  std::vector<unsigned char> pond;     // undermap water
  std::string failure;
};

struct Disjoint {
  std::vector<int> parent;
  explicit Disjoint(int count) : parent(count) {
    for (int i = 0; i < count; ++i)
      parent[i] = i;
  }
  int find(int v) {
    while (parent[v] != v) {
      parent[v] = parent[parent[v]];
      v = parent[v];
    }
    return v;
  }
  bool join(int a, int b) {
    a = find(a);
    b = find(b);
    if (a == b)
      return false;
    parent[b] = a;
    return true;
  }
};

// Distinct valley ids within Chebyshev radius r of a tile, sorted, at most `limit` of them.
int nearbyValleys(const Layout &L, int x, int y, int r, int ids[], int limit) {
  int count = 0;
  for (int dy = -r; dy <= r; ++dy)
    for (int dx = -r; dx <= r; ++dx) {
      const int v = L.valley[L.t.at(x + dx, y + dy)];
      if (v < 0 || std::find(ids, ids + count, v) != ids + count)
        continue;
      if (count == limit)
        return count;
      ids[count++] = v;
    }
  std::sort(ids, ids + count);
  return count;
}

// Whether valleys a and b meet inside the window around (cx, cy), walking through non-ridge
// tiles.
bool joins(const Layout &L, int cx, int cy, int a, int b, int radius) {
  const int side = 2 * radius + 1;
  std::vector<unsigned char> seen(size_t(side) * side, 0);
  std::vector<int> queue;
  for (int dy = -radius; dy <= radius; ++dy)
    for (int dx = -radius; dx <= radius; ++dx)
      if (L.valley[L.t.at(cx + dx, cy + dy)] == a) {
        const int local = (dy + radius) * side + dx + radius;
        seen[local] = 1;
        queue.push_back(local);
      }
  for (size_t head = 0; head < queue.size(); ++head) {
    const int lx = queue[head] % side, ly = queue[head] / side;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int nx = lx + dx, ny = ly + dy;
        if (nx < 0 || ny < 0 || nx >= side || ny >= side || seen[size_t(ny) * side + nx])
          continue;
        const int tile = L.t.at(cx + nx - radius, cy + ny - radius);
        if (L.ridge[tile])
          continue;
        if (L.valley[tile] == b)
          return true;
        seen[size_t(ny) * side + nx] = 1;
        queue.push_back(ny * side + nx);
      }
  }
  return false;
}

// Cuts a pass of the given width through the ridge between valleys a and b. Sites far along the
// ridge from any junction come first, so a pass sits in the middle of its ridgeline; each try is
// kept only if it really joins the two valleys.
bool carvePass(Layout &L, GenerationContext &context, int a, int b, std::vector<int> candidates,
               const std::vector<int> &alongRidge, int width) {
  const Torus &t = L.t;
  candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                  [&](int c) {
                                    if (!L.ridge[c])
                                      return true;
                                    for (const Pass &p : L.passes)
                                      if (t.chebyshev(c % t.w, c / t.w, p.x, p.y) < kPassSpacing)
                                        return true;
                                    return false;
                                  }),
                   candidates.end());
  if (candidates.empty())
    return false;
  const auto score = [&](int c) { return alongRidge[c] < 0 ? kFar : alongRidge[c]; };
  std::stable_sort(candidates.begin(), candidates.end(),
                   [&](int p, int q) { return score(p) > score(q); });
  const std::int64_t best = score(candidates.front());
  size_t band = 0;
  while (band < candidates.size() && std::int64_t(score(candidates[band])) * 4 >= best * 3)
    ++band;
  band = std::max<size_t>(1, band);
  const size_t first = context.bounded("highlands-passes", std::uint32_t(band));
  const int lo = -(width - 1) / 2, hi = width / 2;
  for (size_t k = 0; k < candidates.size() && k < 12; ++k) {
    const int c = candidates[k < band ? (first + k) % band : k];
    const int cx = c % t.w, cy = c / t.w;
    std::vector<int> cleared;
    for (int dy = lo; dy <= hi; ++dy)
      for (int dx = lo; dx <= hi; ++dx) {
        const int i = t.at(cx + dx, cy + dy);
        if (L.ridge[i]) {
          L.ridge[i] = 0;
          cleared.push_back(i);
        }
      }
    if (joins(L, cx, cy, a, b, width + 4)) {
      L.passes.push_back({cx, cy, lo, hi, a, b});
      for (int dy = lo; dy <= hi; ++dy)
        for (int dx = lo; dx <= hi; ++dx)
          L.passTile[t.at(cx + dx, cy + dy)] = 1;
      return true;
    }
    for (int i : cleared)
      L.ridge[i] = 1;
  }
  return false;
}

// Homes by farthest-point spreading over the roomier valleys, one colony per valley while they
// last, then anywhere deep enough and well away from every other home. A valley's home site sits
// out from its deepest point, leaving that interior for the pond beside it.
bool chooseHomes(Layout &L, GenerationContext &context, int teams) {
  const Torus &t = L.t;
  std::vector<int> site(L.valleys, -1), room(L.valleys, 0), rooms;
  for (int v = 0; v < L.valleys; ++v) {
    for (int i : L.tilesOf[v])
      if (L.ridgeDistance[i] >= kRidgeRoad)
        ++room[v];
    const int pole = L.pole[v];
    if (pole < 0 || L.ridgeDistance[pole] < kHomeRidge)
      continue;
    // About halfway out from the pole towards the ridge: room to build on the ridge side, and
    // the valley's interior left for the pond on the other.
    const int depth = std::max(kHomeRidge, L.ridgeDistance[pole] / 2);
    const int offset = std::min(kHomeOffset, L.ridgeDistance[pole] - depth);
    int bestKey = INT_MAX;
    for (int i : L.tilesOf[v]) {
      if (L.ridgeDistance[i] < depth)
        continue;
      const double d = std::sqrt(double(t.dist2(i % t.w, i / t.w, pole % t.w, pole / t.w)));
      const int key = std::abs(int(std::lround(d)) - offset) * 64 - L.ridgeDistance[i];
      if (key < bestKey) {
        bestKey = key;
        site[v] = i;
      }
    }
    rooms.push_back(room[v]);
  }
  if (rooms.empty()) {
    L.failure = "no valley is deep enough for a colony";
    return false;
  }
  // Colonies take the roomiest valleys, the 2 x colonies largest and none under half the median,
  // so no colony starts cramped while a roomier valley goes spare.
  std::vector<int> ranked = rooms;
  std::sort(ranked.begin(), ranked.end(), std::greater<int>());
  const int roomFloor =
      std::max(percentile(rooms, 50) / 2,
               ranked[std::min(ranked.size(), size_t(2 * std::max(1, teams))) - 1]);
  std::vector<int> primary;
  for (int v = 0; v < L.valleys; ++v)
    if (site[v] >= 0 && room[v] >= roomFloor)
      primary.push_back(v);
  std::vector<unsigned char> used(L.valleys, 0);
  const auto spread = [&](int x, int y) {
    int nearest = INT_MAX;
    for (const Home &h : L.homes)
      nearest = std::min(nearest, t.dist2(x, y, h.x, h.y));
    return nearest;
  };
  const auto take = [&](int tile) {
    used[L.valley[tile]] = 1;
    L.homes.push_back({L.valley[tile], tile % t.w, tile / t.w});
  };
  if (teams > 0 && !primary.empty())
    take(site[primary[context.bounded("highlands-homes", std::uint32_t(primary.size()))]]);
  while (int(L.homes.size()) < teams) {
    int chosen = -1, chosenSpread = -1;
    for (int v : primary) {
      if (used[v])
        continue;
      const int s = spread(site[v] % t.w, site[v] / t.w);
      if (s > chosenSpread || (s == chosenSpread && room[v] > room[L.valley[chosen]])) {
        chosen = site[v];
        chosenSpread = s;
      }
    }
    if (chosen < 0) {
      for (int v = 0; v < L.valleys; ++v)
        for (int i : L.tilesOf[v]) {
          const int x = i % t.w, y = i / t.w;
          if (((x | y) & 1) || L.ridgeDistance[i] < kHomeRidge)
            continue;
          const int s = spread(x, y);
          if (s > chosenSpread) {
            chosen = i;
            chosenSpread = s;
          }
        }
      if (chosenSpread < kHomeSeparation * kHomeSeparation)
        chosen = -1;
    }
    if (chosen < 0) {
      L.failure = "no room for colony " + std::to_string(L.homes.size()) + " in any valley";
      return false;
    }
    take(chosen);
  }
  return true;
}

// Grows one pond from its seed, always taking the lowest point of the depth field next, so it
// fills its hollow the way water would. Distance from the seed counts as depth too, which keeps
// the outline round rather than following the noise lattice into points.
void growPond(Layout &L, const std::vector<int> &depth, const std::vector<int> &eligible,
              int eligibleStamp, std::vector<int> &queued, int queuedStamp, int seed, int target) {
  using Entry = std::pair<int, int>;
  const int sx = seed % L.t.w, sy = seed / L.t.w;
  const auto key = [&](int tile) {
    const double d = std::sqrt(double(L.t.dist2(sx, sy, tile % L.t.w, tile / L.t.w)));
    return depth[tile] + int(std::lround(d * 1500));
  };
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;
  frontier.push({key(seed), seed});
  queued[seed] = queuedStamp;
  static const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  int grown = 0;
  while (!frontier.empty() && grown < target) {
    const int tile = frontier.top().second;
    frontier.pop();
    if (L.pond[tile])
      continue;
    L.pond[tile] = 1;
    ++grown;
    for (const auto &s : steps) {
      const int next = L.t.at(tile % L.t.w + s[0], tile / L.t.w + s[1]);
      if (eligible[next] == eligibleStamp && !L.pond[next] && queued[next] != queuedStamp) {
        queued[next] = queuedStamp;
        frontier.push({key(next), next});
      }
    }
  }
}

void placePonds(Layout &L, GenerationContext &context, int spacing, int pondSize) {
  const Torus &t = L.t;
  const int n = t.w * t.h;
  const std::vector<int> depth =
      fractalNoise(t.w, t.h, std::max(8, spacing / 2), 2, context.stream("highlands-ponds"));
  L.pond.assign(n, 0);
  // Ground kept for every colony, and the home each valley's first pond is placed beside.
  std::vector<unsigned char> reserved(n, 0);
  std::vector<int> homeOf(L.valleys, -1);
  for (size_t k = 0; k < L.homes.size(); ++k) {
    const Home &h = L.homes[k];
    if (homeOf[h.valley] < 0)
      homeOf[h.valley] = int(k);
    for (int dy = -kHomePondGap; dy <= kHomePondGap; ++dy)
      for (int dx = -kHomePondGap; dx <= kHomePondGap; ++dx)
        if (dx * dx + dy * dy < kHomePondGap * kHomePondGap)
          reserved[t.at(h.x + dx, h.y + dy)] = 1;
    // And a five-wide way out, straight down to the band along the ridges, so however the ponds
    // grow around a home they can never close it in.
    int x = h.x, y = h.y;
    while (L.ridgeDistance[t.at(x, y)] > 2) {
      int nextX = x, nextY = y;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          if (L.ridgeDistance[t.at(x + dx, y + dy)] < L.ridgeDistance[t.at(nextX, nextY)]) {
            nextX = x + dx;
            nextY = y + dy;
          }
      x = nextX;
      y = nextY;
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
          reserved[t.at(x + dx, y + dy)] = 1;
    }
  }
  std::vector<int> eligible(n, -1), queued(n, -1);
  int queuedStamp = 0;
  for (int v = 0; v < L.valleys; ++v) {
    const std::vector<int> &tiles = L.tilesOf[v];
    if (int(tiles.size()) < kPondMinimumValley)
      continue;
    std::vector<int> room;
    for (int clearance = kPondClearance;
         clearance >= kPondMinimumClearance && int(room.size()) < kMinimumPond; --clearance) {
      room.clear();
      for (int i : tiles)
        if (L.ridgeDistance[i] >= clearance && !reserved[i])
          room.push_back(i);
    }
    if (int(room.size()) < kMinimumPond)
      continue;
    for (int i : room)
      eligible[i] = v;
    // A colony's pond lies beside it, towards the middle of its valley.
    int targetX = -1, targetY = -1;
    if (homeOf[v] >= 0) {
      const Home &h = L.homes[homeOf[v]];
      const int ox = t.offsetX(h.x, L.pole[v] % t.w), oy = t.offsetY(h.y, L.pole[v] / t.w);
      const double length = std::sqrt(double(ox * ox + oy * oy));
      const double ux = length < 1 ? 0.0 : ox / length, uy = length < 1 ? 1.0 : oy / length;
      targetX = t.x(h.x + int(std::lround(ux * kHomePondReach)));
      targetY = t.y(h.y + int(std::lround(uy * kHomePondReach)));
    }
    const int area = int(tiles.size());
    const int target = std::max(kMinimumPond, area * pondSize / 100);
    const int count = std::max(1, std::min(1 + area / kPondArea, int(room.size()) / (2 * kMinimumPond)));
    const int each = std::max(kMinimumPond, target / count);
    std::vector<int> seeds;
    for (int k = 0; k < count; ++k) {
      const bool besideHome = k == 0 && targetX >= 0;
      const auto better = [&](int i, int current) {
        if (besideHome) {
          const int di = t.dist2(i % t.w, i / t.w, targetX, targetY);
          const int dc = t.dist2(current % t.w, current / t.w, targetX, targetY);
          if (di != dc)
            return di < dc;
        }
        return depth[i] < depth[current];
      };
      // A seed with room all round it, where there is one, so the pond has somewhere to grow.
      const auto interior = [&](int i) {
        for (int dy = -2; dy <= 2; ++dy)
          for (int dx = -2; dx <= 2; ++dx)
            if (eligible[t.at(i % t.w + dx, i / t.w + dy)] != v)
              return false;
        return true;
      };
      int seed = -1;
      for (int pass = 0; pass < 2 && seed < 0; ++pass)
        for (int i : room) {
          if (L.pond[i] || (pass == 0 && !interior(i)))
            continue;
          bool apart = true;
          for (int s : seeds)
            if (t.dist2(i % t.w, i / t.w, s % t.w, s / t.w) < kPondSeparation * kPondSeparation) {
              apart = false;
              break;
            }
          if (apart && (seed < 0 || better(i, seed)))
            seed = i;
        }
      if (seed < 0)
        break;
      seeds.push_back(seed);
      growPond(L, depth, eligible, v, queued, queuedStamp++, seed, each);
    }
  }
  // Fill any ground a pond has closed in: everything still walkable is reachable from the band
  // along the ridges, which no pond comes near.
  std::vector<unsigned char> reached(n, 0);
  std::vector<int> queue;
  for (int i = 0; i < n; ++i)
    if (!L.ridge[i] && !L.pond[i] && L.ridgeDistance[i] <= 2) {
      reached[i] = 1;
      queue.push_back(i);
    }
  for (size_t head = 0; head < queue.size(); ++head) {
    const int p = queue[head], px = p % t.w, py = p / t.w;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int q = t.at(px + dx, py + dy);
        if (!reached[q] && !L.ridge[q] && !L.pond[q]) {
          reached[q] = 1;
          queue.push_back(q);
        }
      }
  }
  for (int i = 0; i < n; ++i)
    if (!L.ridge[i] && !L.pond[i] && !reached[i])
      L.pond[i] = 1;
}

Layout design(const GenerationRequest &request, GenerationContext &context) {
  const StoneHighlandsOptions o(request);
  Layout L;
  L.t = {1 << request.wDec, 1 << request.hDec};
  const Torus &t = L.t;
  const int n = t.w * t.h;

  const std::vector<Site> sites = scatterSites(t, o.valleySize, context);
  const std::vector<int> label = labelCells(t, sites, o.valleySize, context);

  // A tile is ridge when any of its eight neighbours has a lower label. A tile no ridge was
  // painted on then touches only its own label or ridge, so no unit can step, even diagonally,
  // from one cell into another.
  L.ridge.assign(n, 0);
  for (int y = 0; y < t.h; ++y)
    for (int x = 0; x < t.w; ++x) {
      const int i = y * t.w + x;
      for (int dy = -1; dy <= 1 && !L.ridge[i]; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          if (label[t.at(x + dx, y + dy)] < label[i]) {
            L.ridge[i] = 1;
            break;
          }
    }
  // Stretches of ridgeline two tiles thick, where a noise field says so: 45% of the ridge at the
  // default stone amount, which scales that share. More stone never opens a gap.
  const std::vector<int> thickness =
      periodicNoise(t.w, t.h, std::max(4, o.valleySize / 2), context.stream("highlands-ridge"));
  const int thickShare = int(std::min<std::int64_t>(100, scaledCount(45, o.stone)));
  const int thickLevel = percentile(thickness, 100 - thickShare);
  std::vector<unsigned char> thick = L.ridge;
  static const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int i = 0; i < n; ++i) {
    if (!L.ridge[i] || thickShare <= 0 || thickness[i] < thickLevel)
      continue;
    for (const auto &s : steps) {
      const int j = t.at(i % t.w + s[0], i / t.w + s[1]);
      if (label[j] < label[i])
        thick[j] = 1;
    }
  }
  L.ridge.swap(thick);

  // Valleys are the walkable components left between the ridges; pockets too small to live in
  // are filled with stone.
  std::vector<unsigned char> open(n);
  for (int i = 0; i < n; ++i)
    open[i] = !L.ridge[i];
  const std::vector<int> region = connectedRegions(open, t.w, t.h, true, GridNeighbors::Eight);
  int regions = 0;
  for (int r : region)
    regions = std::max(regions, r + 1);
  std::vector<int> size(regions, 0), id(regions, -1);
  for (int r : region)
    if (r >= 0)
      ++size[r];
  for (int r = 0; r < regions; ++r)
    if (size[r] >= kPocketTiles)
      id[r] = L.valleys++;
  L.valley.assign(n, -1);
  for (int i = 0; i < n; ++i)
    if (region[i] >= 0) {
      if (id[region[i]] < 0)
        L.ridge[i] = 1;
      else
        L.valley[i] = id[region[i]];
    }
  if (L.valleys < 1) {
    L.failure = "the ridges left no valley to live in";
    return L;
  }

  // Pass sites: ridge tiles with exactly two valleys close by, and no third one within reach of
  // the cleared box. Junction tiles touch three or more valleys; distance along the ridge from
  // the nearest junction ranks how central a site is on its ridgeline.
  const int reach = o.passWidth / 2 + 2;
  std::map<std::pair<int, int>, std::vector<int>> exclusive, anyPair;
  std::vector<int> alongRidge(n, -1), queue;
  for (int y = 0; y < t.h; ++y)
    for (int x = 0; x < t.w; ++x) {
      const int i = y * t.w + x;
      if (!L.ridge[i])
        continue;
      // Not near/far: both are legacy macros in Windows' windef.h that expand to nothing, which
      // turns `int near[3]` into a structured binding declaration and fails to compile on mingw.
      int nearIds[3], farIds[3];
      const int count = nearbyValleys(L, x, y, 2, nearIds, 3);
      if (count >= 3) {
        alongRidge[i] = 0;
        queue.push_back(i);
      }
      for (int p = 0; p < count; ++p)
        for (int q = p + 1; q < count; ++q)
          anyPair[{nearIds[p], nearIds[q]}].push_back(i);
      if (count == 2 && nearbyValleys(L, x, y, reach, farIds, 3) == 2)
        exclusive[{nearIds[0], nearIds[1]}].push_back(i);
    }
  for (size_t head = 0; head < queue.size(); ++head) {
    const int p = queue[head], px = p % t.w, py = p / t.w;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int q = t.at(px + dx, py + dy);
        if (L.ridge[q] && alongRidge[q] < 0) {
          alongRidge[q] = alongRidge[p] + 1;
          queue.push_back(q);
        }
      }
  }

  // A random spanning tree of passes joins every valley; loopiness then opens that share of the
  // remaining shared ridgelines as well.
  L.passTile.assign(n, 0);
  Disjoint sets(L.valleys);
  int groups = L.valleys;
  std::vector<std::pair<int, int>> edges, spare;
  for (const auto &e : exclusive)
    if (int(e.second.size()) >= kMinimumBoundary)
      edges.push_back(e.first);
  shuffle(edges, context, "highlands-passes");
  for (const auto &e : edges) {
    if (sets.find(e.first) == sets.find(e.second)) {
      spare.push_back(e);
      continue;
    }
    if (carvePass(L, context, e.first, e.second, exclusive[e], alongRidge, o.passWidth)) {
      sets.join(e.first, e.second);
      --groups;
    }
  }
  if (groups > 1) {
    // Some valley only meets its neighbours near a junction; allow a pass there.
    std::vector<std::pair<int, int>> fallback;
    for (const auto &e : anyPair)
      fallback.push_back(e.first);
    shuffle(fallback, context, "highlands-passes");
    for (const auto &e : fallback)
      if (sets.find(e.first) != sets.find(e.second) &&
          carvePass(L, context, e.first, e.second, anyPair[e], alongRidge, o.passWidth)) {
        sets.join(e.first, e.second);
        --groups;
      }
  }
  if (groups > 1) {
    L.failure = "passes could not join every valley (" + std::to_string(groups) + " groups remain)";
    return L;
  }
  const int extra = int((std::int64_t(spare.size()) * o.loopiness + 50) / 100);
  for (size_t k = 0, added = 0; k < spare.size() && int(added) < extra; ++k)
    if (carvePass(L, context, spare[k].first, spare[k].second, exclusive[spare[k]], alongRidge,
                  o.passWidth))
      ++added;

  std::vector<unsigned char> wall(n);
  for (int i = 0; i < n; ++i)
    wall[i] = L.ridge[i] || L.passTile[i];
  L.ridgeDistance = chebyshevDistance(t, wall);
  L.tilesOf.assign(L.valleys, {});
  L.pole.assign(L.valleys, -1);
  for (int i = 0; i < n; ++i) {
    const int v = L.valley[i];
    if (v < 0 || L.passTile[i])
      continue;
    L.tilesOf[v].push_back(i);
    if (L.pole[v] < 0 || L.ridgeDistance[i] > L.ridgeDistance[L.pole[v]])
      L.pole[v] = i;
  }
  if (!chooseHomes(L, context, request.nbTeams))
    return L;
  placePonds(L, context, o.valleySize, o.pondSize);
  for (size_t k = 0; k < L.homes.size(); ++k)
    if (L.pond[t.at(L.homes[k].x, L.homes[k].y)])
      L.failure = "a pond covered the home of colony " + std::to_string(k);
  return L;
}

// Terrain straight from the design: pond water inside a two-tile sand beach, grass everywhere
// else, and stone on every ridge tile. Algae only regrows beside a tile of pure sand, which a
// one-tile beach never makes: every tile takes its terrain from four undermap corners.
bool stampTerrain(Map &map, GenerationContext &context, const Layout &L) {
  const Torus &t = L.t;
  constexpr int kBeach = 2;
  for (int y = 0; y < t.h; ++y)
    for (int x = 0; x < t.w; ++x) {
      const int i = y * t.w + x;
      if (L.pond[i]) {
        map.setUMTerrain(x, y, WATER);
        continue;
      }
      bool shore = false;
      for (int dy = -kBeach; dy <= kBeach && !shore; ++dy)
        for (int dx = -kBeach; dx <= kBeach; ++dx)
          if (L.pond[t.at(x + dx, y + dy)]) {
            shore = true;
            break;
          }
      map.setUMTerrain(x, y, shore ? SAND : GRASS);
    }
  map.rebuildTerrain();
  for (int y = 0; y < t.h; ++y)
    for (int x = 0; x < t.w; ++x) {
      if (!L.ridge[size_t(y) * t.w + x])
        continue;
      if (map.getTerrainType(x, y) != GRASS) {
        context.detail = "ridge tile (" + std::to_string(x) + ", " + std::to_string(y) +
                         ") is not solid grass";
        return false;
      }
      map.setResource(x, y, STONE, 1);
    }
  return true;
}

// Every home gets the same kit: a wheat patch and a wood patch of kHomeKit tiles each, a few
// steps from the swarm on either side of the way down to its pond.
void furnishHome(Map &map, const Layout &L, int bootX, int bootY, int homeValley) {
  const Torus &t = L.t;
  const int cx = bootX + 2, cy = bootY + 2;
  int ox = 0, oy = 1, nearest = INT_MAX;
  for (int dy = -24; dy <= 24; ++dy)
    for (int dx = -24; dx <= 24; ++dx)
      if (L.pond[t.at(cx + dx, cy + dy)] && dx * dx + dy * dy < nearest) {
        nearest = dx * dx + dy * dy;
        ox = dx;
        oy = dy;
      }
  const double length = std::max(1.0, std::sqrt(double(ox * ox + oy * oy)));
  const double ux = ox / length, uy = oy / length;
  const double cosine = std::cos(50.0 * 3.14159265358979 / 180.0);
  const double sine = std::sin(50.0 * 3.14159265358979 / 180.0);
  for (int side : {1, -1}) {
    const double rx = ux * cosine - side * uy * sine, ry = side * ux * sine + uy * cosine;
    const int ax = cx + int(std::lround(rx * kKitReach)), ay = cy + int(std::lround(ry * kKitReach));
    std::vector<std::pair<int, int>> tiles;
    for (int dy = -kKitSearch; dy <= kKitSearch; ++dy)
      for (int dx = -kKitSearch; dx <= kKitSearch; ++dx) {
        const int x = t.x(ax + dx), y = t.y(ay + dy), i = y * t.w + x;
        if (L.valley[i] != homeValley || L.passTile[i] || L.ridgeDistance[i] < kRidgeRoad)
          continue;
        if (!map.isGrass(x, y) || map.isResource(x, y) || map.getBuilding(x, y) != NOGBID ||
            map.getGroundUnit(x, y) != NOGUID)
          continue;
        // Two clear tiles all round the swarm, where its workers stand and walk out.
        const int fx = t.offsetX(bootX, x), fy = t.offsetY(bootY, y);
        const int gapX = fx < 0 ? -fx : std::max(0, fx - 3), gapY = fy < 0 ? -fy : std::max(0, fy - 3);
        if (std::max(gapX, gapY) <= 2)
          continue;
        tiles.push_back({dx * dx + dy * dy, i});
      }
    std::sort(tiles.begin(), tiles.end());
    for (int k = 0; k < kHomeKit && k < int(tiles.size()); ++k)
      map.setResource(tiles[k].second % t.w, tiles[k].second / t.w, side > 0 ? CORN : WOOD, 1);
  }
}

void stampBox(const Torus &t, std::vector<unsigned char> &mask, int cx, int cy, int radius) {
  for (int dy = -radius; dy <= radius; ++dy)
    for (int dx = -radius; dx <= radius; ++dx)
      mask[t.at(cx + dx, cy + dy)] = 1;
}

// Ambient farmland: the most fertile ground around each pond, in patches, split 2:1 between
// wheat and wood by an unrelated noise field so the two crops alternate around the shore.
void scatterFarmland(Map &map, GenerationContext &context, const Layout &L,
                     const std::vector<int> &pondDistance, const std::vector<unsigned char> &keepClear,
                     int wheatPercent, int woodPercent) {
  const Torus &t = L.t;
  const int n = t.w * t.h;
  const Fertility::Field fertility = Fertility::forMap(map, false);
  const std::vector<int> split = periodicNoise(t.w, t.h, 8, context.stream("highlands-farmland"));
  const std::vector<int> patch = fractalNoise(t.w, t.h, 16, 2, context.stream("highlands-farmland"));
  const int patchLevel = percentile(patch, 45);
  std::vector<std::vector<int>> pool(L.valleys);
  std::vector<int> pondTiles(L.valleys, 0);
  for (int i = 0; i < n; ++i) {
    const int v = L.valley[i];
    if (v < 0)
      continue;
    if (L.pond[i]) {
      ++pondTiles[v];
      continue;
    }
    const int x = i % t.w, y = i / t.w;
    if (keepClear[i] || L.ridgeDistance[i] < kRidgeRoad || pondDistance[i] < 2 ||
        pondDistance[i] > 9 || patch[i] < patchLevel || fertility.at(x, y) == 0)
      continue;
    if (!map.isGrass(x, y) || map.isResource(x, y) || map.getBuilding(x, y) != NOGBID ||
        map.getGroundUnit(x, y) != NOGUID)
      continue;
    pool[v].push_back(i);
  }
  for (int v = 0; v < L.valleys; ++v) {
    std::vector<int> &tiles = pool[v];
    const int wanted = std::min(int(std::int64_t(pondTiles[v]) * kFarmlandPercent / 100),
                                int(tiles.size()) * 2 / 3);
    // Two thirds wheat and the rest wood, each scaled by its amount; more than the default may
    // take every eligible tile, keeping the two crops' proportions.
    int wheat = int(scaledCount(wanted * 2 / 3, wheatPercent));
    int total = wheat + int(scaledCount(wanted - wanted * 2 / 3, woodPercent));
    if (total > int(tiles.size())) {
      wheat = int(std::int64_t(wheat) * tiles.size() / total);
      total = int(tiles.size());
    }
    if (total <= 0)
      continue;
    std::stable_sort(tiles.begin(), tiles.end(), [&](int a, int b) {
      return fertility.at(a % t.w, a / t.w) > fertility.at(b % t.w, b / t.w);
    });
    tiles.resize(total);
    std::stable_sort(tiles.begin(), tiles.end(), [&](int a, int b) { return split[a] < split[b]; });
    for (int k = 0; k < total; ++k)
      map.setResource(tiles[k] % t.w, tiles[k] / t.w, k < wheat ? CORN : WOOD, 1);
  }
}

// Algae in the open water of every pond.
void seedAlgae(Map &map, GenerationContext &context, const Layout &L, int algaePercent) {
  const Torus &t = L.t;
  const std::vector<int> noise = periodicNoise(t.w, t.h, 6, context.stream("highlands-algae"));
  std::vector<int> water, levels;
  for (int i = 0; i < t.w * t.h; ++i)
    if (L.pond[i] && map.isWater(i % t.w, i / t.w)) {
      water.push_back(i);
      levels.push_back(noise[i]);
    }
  if (water.empty() || algaePercent <= 0)
    return;
  const int level = percentile(
      levels, int(std::min<std::int64_t>(100, scaledCount(kAlgaePercent, algaePercent))));
  for (int i : water)
    if (noise[i] <= level)
      map.setResource(i % t.w, i / t.w, ALGA, 1);
}

// Fruit is rare: small groves beside the ponds of valleys no colony starts in, a prize for
// whoever holds the passes to them - or in any valley, colonies' own included, if asked.
void plantFruit(Map &map, GenerationContext &context, const Layout &L,
                const std::vector<int> &pondDistance, const std::vector<unsigned char> &keepClear,
                const std::vector<unsigned char> &homeValley, int fruit, bool inHomeValleys) {
  if (fruit <= 0)
    return;
  const Torus &t = L.t;
  const int n = t.w * t.h;
  std::vector<std::vector<int>> pool(L.valleys);
  for (int i = 0; i < n; ++i) {
    const int v = L.valley[i], x = i % t.w, y = i / t.w;
    if (v < 0 || keepClear[i] || L.pond[i] || L.ridgeDistance[i] < kRidgeRoad + 1 ||
        pondDistance[i] < 3 || pondDistance[i] > 8)
      continue;
    if (!map.isGrass(x, y) || map.isResource(x, y) || map.getBuilding(x, y) != NOGBID ||
        map.getGroundUnit(x, y) != NOGUID)
      continue;
    pool[v].push_back(i);
  }
  std::vector<int> valleys;
  for (int pass = 0; pass < 2 && valleys.empty(); ++pass)
    for (int v = 0; v < L.valleys; ++v)
      if (!pool[v].empty() && (pass == 1 || inHomeValleys || !homeValley[v]))
        valleys.push_back(v);
  if (valleys.empty())
    return;
  shuffle(valleys, context, "highlands-fruit");
  const int groves = std::max(1, int(std::int64_t(fruit) * n / 16384));
  const int firstType = int(context.bounded("highlands-fruit", 3));
  for (int g = 0; g < groves; ++g) {
    const std::vector<int> &tiles = pool[valleys[g % valleys.size()]];
    const int anchor = tiles[context.bounded("highlands-fruit", std::uint32_t(tiles.size()))];
    const int ax = anchor % t.w, ay = anchor / t.w;
    std::vector<std::pair<int, int>> grove;
    for (int tile : tiles) {
      const int d = t.dist2(ax, ay, tile % t.w, tile / t.w);
      if (d <= 8 && !map.isResource(tile % t.w, tile / t.w))
        grove.push_back({d, tile});
    }
    std::sort(grove.begin(), grove.end());
    for (int k = 0; k < kFruitGrove && k < int(grove.size()); ++k)
      map.setResource(grove[k].second % t.w, grove[k].second / t.w, CHERRY + (firstType + g) % 3, 1);
  }
}

bool generate(Game &game, GenerationContext &context) {
  context.stage = "highland layout";
  const StoneHighlandsOptions o(context.request);
  Map &map = game.map;
  const int teams = context.request.nbTeams;
  map.makeHomogenMap(GRASS);
  for (int i = 0; i < teams; ++i)
    game.addTeam();
  const Layout L = design(context.request, context);
  if (!L.failure.empty()) {
    context.detail = L.failure;
    return false;
  }
  const Torus &t = L.t;
  const int n = t.w * t.h;

  context.stage = "highland terrain";
  if (!stampTerrain(map, context, L))
    return false;

  context.stage = "highland colonies";
  const std::vector<int> pondDistance = chebyshevDistance(t, L.pond);
  for (int team = 0; team < teams; ++team) {
    const Home &h = L.homes[team];
    std::vector<unsigned char> home(n, 0);
    for (int dy = -10; dy <= 10; ++dy)
      for (int dx = -10; dx <= 10; ++dx) {
        const int i = t.at(h.x + dx, h.y + dy);
        if (L.valley[i] == h.valley && !L.passTile[i] && L.ridgeDistance[i] >= kRidgeRoad &&
            pondDistance[i] >= 3)
          home[i] = 1;
      }
    // placeSettlement measures from the footprint's top-left tile; this centres the 4x4 swarm.
    if (!placeSettlement(game, context, team, home, {h.x - 2, h.y - 2}, "highlands-starts"))
      return false;
  }
  context.stage = "highland resources";
  std::vector<unsigned char> keepClear(n, 0), homeValley(L.valleys, 0);
  for (const Pass &p : L.passes)
    stampBox(t, keepClear, p.x, p.y, p.hi + kRidgeRoad + 1);
  for (int team = 0; team < teams; ++team) {
    stampBox(t, keepClear, context.bootX[team] + 2, context.bootY[team] + 2, kHomeClear);
    homeValley[L.homes[team].valley] = 1;
    furnishHome(map, L, context.bootX[team], context.bootY[team], L.homes[team].valley);
  }
  scatterFarmland(map, context, L, pondDistance, keepClear, o.wheat, o.wood);
  seedAlgae(map, context, L, o.algae);
  plantFruit(map, context, L, pondDistance, keepClear, homeValley, o.fruit, o.homeValleyFruit);
  // The kits above already put wheat and wood a few steps from every swarm; this is only the
  // backstop, and the ridges are designed walls it must never clear.
  guaranteeStartingResources(game, context, 24, 32, 0, &L.ridge);
  // The backstop places what it has to wherever a colony can walk, and that includes passes.
  // Passes and their mouths hold nothing but their own ridge stone.
  for (const Pass &p : L.passes)
    for (int dy = p.lo - 2; dy <= p.hi + 2; ++dy)
      for (int dx = p.lo - 2; dx <= p.hi + 2; ++dx) {
        const int x = t.x(p.x + dx), y = t.y(p.y + dy);
        if (map.isResource(x, y) && map.getResource(x, y).type != STONE)
          map.setNoResource(x, y, 1);
      }
  return true;
}

std::string validateRequest(const GenerationRequest &r) {
  const StoneHighlandsOptions o(r);
  const std::int64_t area = std::int64_t(1) << (r.wDec + r.hDec);
  if (area < 4 * std::int64_t(o.valleySize) * o.valleySize)
    return "These valleys are too big for the map; use a bigger map or smaller valleys.";
  if (area < std::int64_t(kAreaPerColony) * r.nbTeams)
    return "The highlands have too little room for this many colonies; use a bigger map or fewer "
           "colonies.";
  return "";
}

// Checked on the finished world against the rebuilt design: every ridge still carries its stone,
// every pass is open and reachable on foot, and every colony can walk to every other. Water,
// buildings and every resource tile (farmland included) block the flood; units don't.
std::string validateWorld(const Game &game, const GenerationContext &context) {
  GenerationContext replay(context.request);
  const Layout L = design(context.request, replay);
  if (!L.failure.empty())
    return "The highland design could not be rebuilt: " + L.failure;
  const Map &map = game.map;
  const Torus &t = L.t;
  const int n = t.w * t.h, teams = context.request.nbTeams;
  if (map.getW() != t.w || map.getH() != t.h)
    return "The highland design does not match the map size.";
  for (int i = 0; i < n; ++i)
    if (L.ridge[i] && map.getResource(i % t.w, i / t.w).type != STONE)
      return "The ridge at (" + std::to_string(i % t.w) + ", " + std::to_string(i / t.w) +
             ") has lost its stone.";
  const auto walkable = [&](int x, int y) {
    return !map.isWater(x, y) && !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID;
  };
  for (size_t k = 0; k < L.passes.size(); ++k) {
    const Pass &p = L.passes[k];
    for (int dy = p.lo; dy <= p.hi; ++dy)
      for (int dx = p.lo; dx <= p.hi; ++dx)
        if (!walkable(t.x(p.x + dx), t.y(p.y + dy)))
          return "Pass " + std::to_string(k) + " at (" + std::to_string(p.x) + ", " +
                 std::to_string(p.y) + ") is blocked.";
  }
  std::vector<unsigned char> reached(n, 0);
  std::vector<int> queue;
  for (int i = 0; i < n; ++i) {
    const Uint16 gid = map.getGroundUnit(i % t.w, i / t.w);
    if (gid != NOGUID && Unit::GIDtoTeam(gid) == 0) {
      reached[i] = 1;
      queue.push_back(i);
    }
  }
  for (size_t head = 0; head < queue.size(); ++head) {
    const int p = queue[head], px = p % t.w, py = p / t.w;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int x = t.x(px + dx), y = t.y(py + dy), q = y * t.w + x;
        if (!reached[q] && walkable(x, y)) {
          reached[q] = 1;
          queue.push_back(q);
        }
      }
  }
  std::vector<unsigned char> teamReached(teams, 0);
  for (int i = 0; i < n; ++i) {
    const Uint16 gid = map.getGroundUnit(i % t.w, i / t.w);
    if (gid != NOGUID && reached[i] && Unit::GIDtoTeam(gid) < teams)
      teamReached[Unit::GIDtoTeam(gid)] = 1;
  }
  for (int team = 0; team < teams; ++team)
    if (!teamReached[team])
      return "Colony " + std::to_string(team) + " cannot walk to colony 0 over the highlands.";
  // Each pass must be on the colonies' walkable network and lead straight through: walking out
  // of its box, without going far, reaches both valleys it joins.
  for (size_t k = 0; k < L.passes.size(); ++k) {
    const Pass &p = L.passes[k];
    const std::string where = "Pass " + std::to_string(k) + " at (" + std::to_string(p.x) + ", " +
                              std::to_string(p.y) + ")";
    if (!reached[t.at(p.x, p.y)])
      return where + " cannot be reached on foot.";
    const int radius = p.hi + 4, side = 2 * radius + 1;
    std::vector<unsigned char> seen(size_t(side) * side, 0);
    std::vector<int> local;
    for (int dy = p.lo; dy <= p.hi; ++dy)
      for (int dx = p.lo; dx <= p.hi; ++dx) {
        seen[size_t(dy + radius) * side + dx + radius] = 1;
        local.push_back((dy + radius) * side + dx + radius);
      }
    bool sideA = false, sideB = false;
    for (size_t head = 0; head < local.size() && !(sideA && sideB); ++head) {
      const int lx = local[head] % side, ly = local[head] / side;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const int nx = lx + dx, ny = ly + dy;
          if (nx < 0 || ny < 0 || nx >= side || ny >= side || seen[size_t(ny) * side + nx])
            continue;
          const int x = t.x(p.x + nx - radius), y = t.y(p.y + ny - radius);
          if (!walkable(x, y))
            continue;
          seen[size_t(ny) * side + nx] = 1;
          local.push_back(ny * side + nx);
          sideA |= L.valley[y * t.w + x] == p.a;
          sideB |= L.valley[y * t.w + x] == p.b;
        }
    }
    if (!sideA || !sideB)
      return where + " does not lead through to both of its valleys.";
  }
  return "";
}
} // namespace

StoneHighlandsOptions::StoneHighlandsOptions(const GenerationRequest &r)
    : valleySize(r.option("valley-size")), passWidth(r.option("pass-width")),
      loopiness(r.option("loopiness")), pondSize(r.option("pond-size")), fruit(r.option("fruit")),
      homeValleyFruit(r.option("home-valley-fruit") != 0), wheat(r.option("wheat-amount")),
      wood(r.option("wood-amount")), stone(r.option("stone-amount")),
      algae(r.option("algae-amount")) {}

GeneratorDefinition stoneHighlandsDefinition() {
  return {"stone-highlands",
          14,
          "Stone highlands",
          1,
          false,
          {// Average spacing between valley centres, in tiles.
           {"valley-size", "Valley size", 20, 44, 4, 32, ControlGroup::Layout},
           {"pass-width", "Pass width", 3, 5, 1, 4, ControlGroup::Layout},
           // Share of the ridgelines left over after the spanning tree that get a pass as well.
           {"loopiness", "Loopiness", 0, 100, 5, 30, ControlGroup::Layout},
           // Pond area as a percentage of each valley's area.
           {"pond-size", "Pond size", 4, 16, 2, 8, ControlGroup::Terrain},
           // Fruit groves per 128x128 tiles of map.
           {"fruit", "Fruit", 0, 12, 1, 4, ControlGroup::Resources},
           // On, fruit groves may grow in the valleys colonies start in too.
           GeneratorControl::toggle("home-valley-fruit", "Fruit in home valleys", false,
                                    ControlGroup::Resources),
           // Wheat and wood scale the farmland around the ponds and algae the ponds' own; stone
           // scales how much of the ridgeline is two tiles thick (45% at 100). Every colony's
           // starting kit stays as it is.
           GeneratorControl::percentage("wheat-amount", "Wheat amount"),
           GeneratorControl::percentage("wood-amount", "Wood amount"),
           GeneratorControl::percentage("stone-amount", "Stone amount", 200),
           GeneratorControl::percentage("algae-amount", "Algae amount")},
          generate,
          true,
          validateRequest,
          validateWorld};
}
