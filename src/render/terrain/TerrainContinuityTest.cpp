// SPDX-License-Identifier: GPL-3.0-or-later
// Boundary continuity: whatever terrains meet, the coverage a cell computes on
// its edge must equal what its neighbour computes on the same line. Every
// material in the catalogue takes part, so a new terrain inherits the check.
#include "EngineFixtures.h"
#include "TerrainMaterials.h"
#include "TerrainCompositor.h"
#include "TerrainStones.h"
#include "map/TerrainTypeTable.h"
#include "render/scene/SceneMap.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <tuple>
#include <random>
#include <sstream>
#include <vector>

namespace
{
using TerrainVisual::Coverage;
using TerrainVisual::MaterialId;

// Weight of each material in a coverage sample.
std::map<MaterialId, int> weights(const Coverage &c)
{
	std::map<MaterialId, int> out;
	for (unsigned i = 0; i < 4; ++i)
		if (c.weight[i])
			out[c.material[i]] += int(c.weight[i]);
	return out;
}
int largestDifference(const Coverage &a, const Coverage &b)
{
	auto wa = weights(a), wb = weights(b);
	int worst = 0;
	for (const auto &[m, w] : wa)
		worst = std::max(worst, std::abs(w - (wb.count(m) ? wb[m] : 0)));
	for (const auto &[m, w] : wb)
		if (!wa.count(m))
			worst = std::max(worst, w);
	return worst;
}

// Random blobs, bands and blocks of every paintable terrain over a base.
void paintRandom(Map &map, unsigned seed, int size)
{
	std::vector<TerrainType> types;
	for (unsigned t = 0; t < TERRAIN_COUNT; ++t)
		if (terrainPaintable(TerrainType(t)))
			types.push_back(TerrainType(t));
	std::mt19937 rng(seed);
	const auto pick = [&] { return types[rng() % types.size()]; };
	const auto base = pick();
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x)
			map.setVertexTerrain(x, y, base);
	for (int blob = 0; blob < 30; ++blob)
	{
		const auto type = pick();
		const int cx = int(rng() % unsigned(size)), cy = int(rng() % unsigned(size)), r = 1 + int(rng() % 5);
		const int shape = int(rng() % 4);
		for (int y = cy - r; y <= cy + r; ++y)
			for (int x = cx - r; x <= cx + r; ++x)
			{
				const int dx = x - cx, dy = y - cy;
				const bool inside = shape == 0 ? dx * dx + dy * dy <= r * r
					: shape == 1 ? std::abs(dx - dy) <= r / 2
					: shape == 2 ? std::abs(dx) <= r && std::abs(dy) <= r / 2
					: (rng() % 3) == 0; // scattered single vertices
				if (inside)
					map.setVertexTerrain(x & (size - 1), y & (size - 1), type);
			}
	}
}
} // namespace

TEST_SUITE("TerrainContinuity")
{
	TEST_CASE("coverage agrees across every tile edge for random terrain combinations [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		const auto &compositor = globals->terrainCompositor();
		const auto &catalog = compositor.catalog();
		// GLOB2_TERRAIN_CONTINUITY_SEEDS=first,count widens the search locally.
		unsigned first = 1, count = 6;
		if (const char *seeds = std::getenv("GLOB2_TERRAIN_CONTINUITY_SEEDS"))
			std::sscanf(seeds, "%u,%u", &first, &count);
		// 1% of full coverage: feathered edges are steep, so a real cut or a
		// different contour on either side differs by far more.
		constexpr int Tolerance = 656;
		unsigned edges = 0, broken = 0, sums = 0, marginBroken = 0, clipped = 0, banded = 0, marginFullPalette = 0;
		std::map<std::string, unsigned> marginPairs;
		std::map<std::string, unsigned> brokenPairs;
		std::ostringstream examples;
		for (unsigned seed = first; seed < first + count; ++seed)
		{
			glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
			paintRandom(fixture.game.map, seed, 32);
			SceneMap scene;
			glob2test::observeMap(fixture.game.map, scene);
			std::vector<TerrainVisual::Recipe> recipes(32 * 32);
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
					recipes[y * 32 + x] = compositor.describe(scene, x, y);
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
				{
					const TerrainVisual::PreparedCoverage self(catalog, recipes[y * 32 + x]);
					const TerrainVisual::PreparedCoverage right(catalog, recipes[y * 32 + ((x + 1) & 31)]);
					const TerrainVisual::PreparedCoverage below(catalog, recipes[((y + 1) & 31) * 32 + x]);
					for (int along = 128; along < 8192; along += 256)
					{
						for (bool vertical : {true, false})
						{
							const auto a = vertical ? self.at(8192, along) : self.at(along, 8192);
							const auto b = vertical ? right.at(0, along) : below.at(along, 0);
							++edges;
							for (const auto *c : {&a, &b})
							{
								unsigned total = 0;
								for (unsigned i = 0; i < 4; ++i) total += c->weight[i];
								sums += total != 65536;
							}
							// Seams and surf read the margin and neighbour; every band
							// ends within eight pixels of the contour.
							if (std::min(a.margin, b.margin) < 2048)
							{
								++banded;
								const auto &ra = recipes[y * 32 + x];
								const auto &rb = vertical ? recipes[y * 32 + ((x + 1) & 31)] : recipes[((y + 1) & 31) * 32 + x];
								if (ra.uniform() != rb.uniform())
									++clipped; // The uniform side draws no band at all.
								else if (std::abs(int(a.margin) - int(b.margin)) > 256 || a.neighbor != b.neighbor)
								{
									++marginBroken;
									std::array<TerrainVisual::MaterialId, 4> pa, pb;
									if (ra.palette(pa) == 4 || rb.palette(pb) == 4)
										++marginFullPalette;
									++marginPairs[catalog.materials[a.neighbor].key + "/" + catalog.materials[b.neighbor].key];
								}
							}
							if (largestDifference(a, b) > Tolerance)
							{
								++broken;
								const auto key = [&](const Coverage &c)
								{
									std::string s;
									for (const auto &[m, w] : weights(c))
										s += (s.empty() ? "" : "+") + catalog.materials[m].key;
									return s;
								};
								++brokenPairs[key(a) + " | " + key(b)];
								if (broken <= 8)
									examples << "  seed " << seed << " cell " << x << "," << y
											 << (vertical ? " right" : " bottom") << " edge at " << along << ": "
											 << key(a) << " vs " << key(b) << " (difference "
											 << largestDifference(a, b) << ")\n";
							}
						}
					}
				}
		}
		std::ostringstream summary;
		std::vector<std::pair<unsigned, std::string>> ranked;
		for (const auto &[pair, n] : brokenPairs) ranked.emplace_back(n, pair);
		std::sort(ranked.rbegin(), ranked.rend());
		for (std::size_t i = 0; i < ranked.size() && i < 12; ++i)
			summary << "  " << ranked[i].first << "  " << ranked[i].second << "\n";
		{
			std::ofstream report(glob2test::artifactDir() / "continuity.txt");
			report << broken << " of " << edges << " edge samples disagree; " << sums << " bad sums; "
				   << banded << " samples within a band; " << marginBroken << " seam/surf margins disagree (" << marginFullPalette
				   << " beside a full palette); " << clipped << " bands clipped by a uniform cell\n";
			for (const auto &[k, n] : marginPairs) report << "  margin " << n << "  " << k << "\n";
			report << examples.str() << summary.str();
		}
		MESSAGE(broken << " of " << edges << " edge samples disagree across tiles\n"
					   << examples.str() << "most frequent:\n" << summary.str());
		CHECK(sums == 0);
		CHECK(broken == 0);
		// Seam and surf bands follow the margin; a jump steps them at the tile edge.
		// The margin checks only mean something where bands are drawn.
		CHECK(banded > edges / 50);
		CHECK(marginBroken == 0);
		CHECK(clipped == 0);
	}

	TEST_CASE("laid stones are wholly kept or dropped wherever they are drawn, whatever meets them [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		const auto &compositor = globals->terrainCompositor();
		const auto &catalog = compositor.catalog();
		const TerrainVisual::StoneMaps maps(catalog);
		unsigned first = 1, count = 6;
		if (const char *seeds = std::getenv("GLOB2_TERRAIN_CONTINUITY_SEEDS"))
			std::sscanf(seeds, "%u,%u", &first, &count);
		constexpr int scale = 2, n = 32 * scale;
		// Per world stone (material, centre cell, number): which states its face
		// texels were drawn in (1 dropped, 2 kept, 4 partly covered), and how the
		// cells drew it (1 no stone layer, 2 its own layer, 4 another path's only;
		// 8 and 16 its layer's kept and dropped bit).
		unsigned stonesSeen = 0, mixed = 0, cutByTile = 0;
		std::map<std::string, unsigned> mixedMaterials;
		for (unsigned seed = first; seed < first + count; ++seed)
		{
			glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
			paintRandom(fixture.game.map, seed, 32);
			SceneMap scene;
			glob2test::observeMap(fixture.game.map, scene);
			std::map<std::tuple<TerrainVisual::MaterialId, int, int, int>, unsigned> states, layers;
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
				{
					const auto recipe = compositor.describe(scene, x, y);
					if (recipe.uniform())
						continue;
					const auto mask = compositor.mask(recipe, scale);
					std::array<bool, 4> cutting{};
					for (unsigned slot = 0; slot < mask.colors; ++slot)
						cutting[slot] = catalog.cutsStones(mask.palette[slot]);
					for (unsigned slot = 0; slot < mask.colors; ++slot)
					{
						const auto material = mask.palette[slot];
						const auto *stones = maps.get(material);
						if (!stones)
							continue;
						const unsigned wrap = unsigned(stones->tiles() - 1);
						for (int ty = 0; ty < n; ++ty)
							for (int tx = 0; tx < n; ++tx)
							{
								const int px = tx * 256 / scale + 128 / scale, py = ty * 256 / scale + 128 / scale;
								const auto texel = stones->at(int(unsigned(x) & wrap), int(unsigned(y) & wrap), px, py);
								if (texel.face < int(TerrainVisual::StoneRimQ8))
									continue; // Joints and the feathered rim.
								const auto &sample = mask.samples[std::size_t(ty) * n + tx];
								const unsigned largest = sample.slots & 3;
								std::array<unsigned, 4> weight{};
								unsigned rest = 65536;
								for (unsigned k = 0, other = 0; k < 4; ++k)
									if (k != largest) { weight[k] = sample.others[other++]; rest -= weight[k]; }
								weight[largest] = rest;
								bool cut = false;
								for (unsigned k = 0; k < mask.colors; ++k) cut |= cutting[k] && weight[k];
								if (cut)
									continue; // Water, lava, holes and chasms cut stones by design.
								const auto key = std::tuple{material, (x + texel.ownTile % 3 - 1) & 31,
															(y + texel.ownTile / 3 - 1) & 31, int(texel.own)};
								const unsigned w = weight[slot];
								// Where the dominant path's kept stones lie over this one, the
								// subordinate is cut along their outline by design.
								bool overlaid = false;
								for (unsigned k = 0; k < mask.colors; ++k)
									overlaid |= k != slot && weight[k] && maps.has(mask.palette[k]);
								if (overlaid && w < 65536 - 64)
									continue;
								states[key] |= w == 0 ? 1u : w >= 65536 - 64 ? 2u : 4u;
								// Whether this cell drew the material as its stone layer, as
								// another path's ground, or without stones (the palette limit).
								bool layered = false;
								for (unsigned l = 0; l < recipe.stoneLayerCount; ++l)
									layered |= recipe.stoneLayers[l].material == material;
								layers[key] |= !recipe.stones ? 1u : layered ? 2u : 4u;
								for (unsigned l = 0; l < recipe.stoneLayerCount; ++l)
									if (recipe.stoneLayers[l].material == material)
										layers[key] |= (recipe.stoneLayers[l].kept[texel.ownTile] >> texel.own & 1u) ? 8u : 16u;
							}
					}
				}
			for (const auto &[key, state] : states)
			{
				++stonesSeen;
				// Kept somewhere and dropped or partly drawn elsewhere: a cut.
				if ((state & 2) && (state & 5))
				{
					++mixed;
					const unsigned layer = layers[key];
					++mixedMaterials[catalog.materials[std::get<0>(key)].key +
									 (layer & 4 ? " (another path's layer)" : layer & 1 ? " (no stone layer)"
									  : (layer & 24) == 24 ? " (same layer, cells disagree on its bit)" : layer & 8 ? " (kept bit, drawn missing)" : " (dropped bit, drawn)")];
				}
				cutByTile += state == 4;
			}
		}
		{
			std::ofstream report(glob2test::artifactDir() / "stones.txt");
			report << stonesSeen << " stones; " << mixed << " kept in part only; " << cutByTile
				   << " only ever partly drawn\n";
			for (const auto &[k, v] : mixedMaterials) report << "  " << v << "  " << k << "\n";
		}
		MESSAGE(stonesSeen << " stones; " << mixed << " kept in part only");
		CHECK(stonesSeen > 1000);
		CHECK(mixed == 0);
	}
}
