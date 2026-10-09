#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include "TerrainMaterials.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <algorithm>
#include <cmath>
namespace
{
TerrainVisual::Catalog catalog()
{
	std::ifstream f("data/terrain/tileset.json"); nlohmann::json j; f >> j; return TerrainVisual::Catalog::parse(j);
}
template <class Field>
TerrainVisual::Recipe contextualRecipe(int x, int y, int size, Field field)
{
	TerrainVisual::Recipe r;
	r.x = (x % size + size) % size; r.y = (y % size + size) % size;
	r.width = r.height = size; r.hasNeighborhood = true;
	for (int dy = -1; dy <= 2; ++dy)
		for (int dx = -1; dx <= 2; ++dx)
			r.neighborhood[(dy + 1) * 4 + dx + 1] =
				field((r.x + dx + size) % size, (r.y + dy + size) % size);
	r.corners = {r.neighborhood[5], r.neighborhood[6], r.neighborhood[9], r.neighborhood[10]};
	return r;
}
std::array<unsigned, 64> materialWeights(const TerrainVisual::Coverage &coverage)
{
	std::array<unsigned, 64> weights{};
	for (unsigned k = 0; k < 4; ++k) weights[coverage.material[k]] += coverage.weight[k];
	return weights;
}
} // namespace
TEST_SUITE("TerrainMaterials")
{
	TEST_CASE("contextual borders preserve seams and normalization across both wrapped axes")
	{
		const auto c = catalog();
		const auto a = c.find("sand"), b = c.find("water");
		for (int size : {1, 2, 16})
			for (unsigned seed : {0u, 73u})
				for (int axis = 0; axis < 2; ++axis)
					for (int position = 0; position < size; ++position)
					{
						const auto field = [&](int x, int y)
						{ return TerrainVisual::hash(x, y, seed) & 1 ? a : b; };
						auto r = contextualRecipe(position, position, size, field);
						auto s = contextualRecipe(position + (axis == 0), position + (axis == 1), size, field);
						r.seed = s.seed = seed;
						const TerrainVisual::PreparedCoverage p(c, r), q(c, s);
						for (int t = 0; t <= 8192; t += 64)
						{
							CHECK(materialWeights(axis ? p.at(t, 8192) : p.at(8192, t)) ==
								materialWeights(axis ? q.at(t, 0) : q.at(0, t)));
							const auto sample = p.at(t, 4096);
							CHECK(sample.weight[0] + sample.weight[1] + sample.weight[2] + sample.weight[3] == 65536);
						}
					}
	}
	TEST_CASE("contextual contours use their halo and leave ambiguous and sharp borders unchanged")
	{
		auto c = catalog(); c.boundaryWarp = {};
		const auto a = c.find("sand"), b = c.find("water");
		auto additional = c.materials[c.find("grass")];
		additional.key = "fixture-natural";
		c.materials.push_back(additional); // Sharp borders also remain sharp against new materials.
		for (const auto &material : c.materials)
			for (auto sharp : {"road", "boardwalk", "lava", "ember_field", "void_hole", "chasm"})
				CHECK_FALSE(c.contextualFor(c.find(sharp), c.find(material.key)));
		auto old = c;
		for (auto &profile : old.profiles) profile.contextual = false;
		auto r = contextualRecipe(3, 4, 16, [&](int x, int y) { return x + y < 8 ? a : b; });
		auto changedHalo = r;
		changedHalo.neighborhood[1] = b;
		changedHalo.neighborhood[2] = b;
		CHECK(r.corners == changedHalo.corners);
		CHECK_FALSE(r == changedHalo);
		const TerrainVisual::PreparedCoverage p(c, r), q(c, changedHalo), baseline(old, r);
		unsigned changed = 0, haloChanged = 0;
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
			{
				const int px = x * 256 + 128, py = y * 256 + 128;
				changed += materialWeights(p.at(px, py)) != materialWeights(baseline.at(px, py));
				haloChanged += materialWeights(p.at(px, py)) != materialWeights(q.at(px, py));
			}
		CHECK(changed > 40);
		CHECK(haloChanged > 0);
		for (const auto corners : {std::array<TerrainVisual::MaterialId, 4>{a, b, b, a},
			std::array<TerrainVisual::MaterialId, 4>{a, b, c.find("grass"), a},
			std::array<TerrainVisual::MaterialId, 4>{c.find("road"), a, c.find("road"), a}})
		{
			r.corners = corners;
			r.neighborhood[5] = corners[0]; r.neighborhood[6] = corners[1];
			r.neighborhood[9] = corners[2]; r.neighborhood[10] = corners[3];
			const TerrainVisual::PreparedCoverage current(c, r), original(old, r);
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
					CHECK(materialWeights(current.at(x * 256 + 128, y * 256 + 128)) ==
						materialWeights(original.at(x * 256 + 128, y * 256 + 128)));
		}
	}
	TEST_CASE("contextual cells retain every corner region at native and HD sample positions")
	{
		auto c = catalog(); c.boundaryWarp = {};
		const auto a = c.find("sand"), b = c.find("water");
		for (unsigned pattern = 1; pattern < 15; ++pattern)
		{
			auto r = contextualRecipe(3, 3, 16, [&](int x, int y)
			{ return TerrainVisual::hash(x, y, pattern) & 1 ? a : b; });
			for (unsigned k = 0; k < 4; ++k)
				r.corners[k] = (pattern >> k) & 1 ? a : b;
			r.neighborhood[5] = r.corners[0]; r.neighborhood[6] = r.corners[1];
			r.neighborhood[9] = r.corners[2]; r.neighborhood[10] = r.corners[3];
			const TerrainVisual::PreparedCoverage p(c, r);
			for (unsigned k = 0; k < 4; ++k)
				CHECK(materialWeights(p.at((k & 1 ? 28 : 4) * 256,
					(k & 2 ? 28 : 4) * 256))[r.corners[k]] == 65536);
			for (int y = 0; y < 128; ++y)
				for (int x = 0; x < 128; ++x)
				{
					const auto sample = p.at(x * 64 + 32, y * 64 + 32);
					const auto weights = materialWeights(sample);
					CHECK(weights[a] + weights[b] == 65536);
				}
			// Deep lobes must preserve the corner pockets for rerolled looks too.
			for (unsigned seed = 1; seed <= 128; ++seed)
			{
				r.seed = seed;
				const TerrainVisual::PreparedCoverage varied(c, r);
				for (unsigned k = 0; k < 4; ++k)
					CHECK(materialWeights(varied.at((k & 1 ? 28 : 4) * 256,
						(k & 2 ? 28 : 4) * 256))[r.corners[k]] == 65536);
			}
		}
	}
	TEST_CASE("contextual interiors remain within twelve pixels of the marching contour")
	{
		auto c = catalog(); c.boundaryWarp = {1024, 384, 128};
		const auto a = c.find("sand"), b = c.find("water");
		struct Point { float x, y; };
		const Point midpoints[] = {{16, 0}, {32, 16}, {16, 32}, {0, 16}};
		for (unsigned pattern = 1; pattern < 15; ++pattern)
			for (unsigned seed : {0u, 19u, 73u})
			{
				auto r = contextualRecipe(3, 7, 16, [&](int x, int y)
				{ return TerrainVisual::hash(x, y, seed) & 1 ? a : b; });
				r.seed = seed;
				for (unsigned k = 0; k < 4; ++k) r.corners[k] = (pattern >> k) & 1 ? a : b;
				r.neighborhood[5] = r.corners[0]; r.neighborhood[6] = r.corners[1];
				r.neighborhood[9] = r.corners[2]; r.neighborhood[10] = r.corners[3];
				const TerrainVisual::MaterialId around[] = {r.corners[0], r.corners[1], r.corners[3], r.corners[2]};
				Point ends[4]; unsigned count = 0;
				for (unsigned k = 0; k < 4; ++k)
					if (around[k] != around[(k + 1) % 4]) ends[count++] = midpoints[k];
				if (count != 2) continue; // Ambiguous diagonals intentionally keep their old shape.
				const float dx = ends[1].x - ends[0].x, dy = ends[1].y - ends[0].y;
				const float length = std::sqrt(dx*dx + dy*dy);
				const float cornerSide = dx * -ends[0].y - dy * -ends[0].x;
				const TerrainVisual::PreparedCoverage p(c, r);
				for (int y = 4; y < 28; ++y)
					for (int x = 4; x < 28; ++x)
					{
						const float cross = dx * (y + .5f - ends[0].y) - dy * (x + .5f - ends[0].x);
						if (std::abs(cross) / length <= 12) continue;
						const auto expected = cross * cornerSide > 0 ? r.corners[0] :
							(r.corners[0] == a ? b : a);
						CHECK(materialWeights(p.at(x * 256 + 128, y * 256 + 128))[expected] >= 32768);
					}
			}
	}
	TEST_CASE("straight contextual borders have seeded scallops without folding")
	{
		auto c = catalog(); c.boundaryWarp = {};
		const auto a = c.find("sand"), b = c.find("grass");
		std::array<std::set<std::vector<int>>, 2> shapes;
		for (unsigned seed : {0u, 19u, 73u, 291u})
			for (int cell = 1; cell < 8; ++cell)
				for (int axis = 0; axis < 2; ++axis)
				{
					auto r = contextualRecipe(axis ? cell : 3, axis ? 3 : cell, 16,
						[&](int x, int y) { return (axis ? y : x) <= 3 ? a : b; });
					r.seed = seed;
					const TerrainVisual::PreparedCoverage p(c, r), repeated(c, r);
					std::vector<int> crossings;
					for (int y = 4; y < 28; ++y)
					{
						int crossing = -1;
						bool enteredB = false;
						for (int x = 0; x < 128; ++x)
						{
							const int px = axis ? y * 256 + 128 : x * 64 + 32;
							const int py = axis ? x * 64 + 32 : y * 256 + 128;
							const auto sample = p.at(px, py);
							CHECK(materialWeights(sample) == materialWeights(repeated.at(px, py)));
							const bool isB = materialWeights(sample)[b] > 32768;
							if (isB && !enteredB) crossing = x;
							CHECK_FALSE((enteredB && !isB));
							enteredB |= isB;
						}
						REQUIRE(crossing >= 0);
						crossings.push_back(crossing);
					}
					const auto [low, high] = std::minmax_element(crossings.begin(), crossings.end());
					CHECK(*high - *low >= 4); // At least one logical pixel of visible variation without warp.
					shapes[axis].insert(crossings);
				}
		for (const auto &axis : shapes) CHECK(axis.size() == 28);
	}

}
