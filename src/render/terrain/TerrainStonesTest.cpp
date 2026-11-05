// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "TerrainStones.h"
#include "TerrainCompositor.h"
#include "TerrainCatalogIO.h"
#include "render/scene/SceneMap.h"
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace
{
// One 128-texel cell holding two stones: the left half is stone 0, the right
// half stone 1, both centred in the cell, with a joint at x = 64.
std::vector<std::uint8_t> twoStones()
{
	constexpr int size = TerrainVisual::StoneMap::TileTexels;
	// Two blocks side by side; no texel has a second neighbour in reach.
	std::vector<std::uint8_t> rgba(std::size_t(size) * size * 8);
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x)
		{
			auto *t = &rgba[(std::size_t(y) * 2 * size + x) * 4];
			auto *u = t + 4 * size;
			u[0] = std::uint8_t(x >= 64);
			u[1] = 4;
			u[3] = 255;
			// A joint two texels wide on either side of x = 64: faces start
			// a texel and a half from it, eight units per texel.
			const int beyond = x >= 64 ? x - 64 : 63 - x;
			u[2] = std::uint8_t(128 + (2 * beyond - 3) * 4);
			const int own = x >= 64, distance = own ? x - 64 : 63 - x; // Texels beyond the joint.
			const bool near = distance < 16;
			t[0] = std::uint8_t(own);
			t[1] = std::uint8_t(4 << 4 | 4); // Both stones centred in this cell.
			t[2] = std::uint8_t(near ? 1 - own : own);
			// Native pixels x 32, joint halfway between texels: (distance + 0.5) / 4.
			t[3] = near ? std::uint8_t((2 * distance + 1) * 4) : 255;
		}
	return rgba;
}
} // namespace

TEST_SUITE("TerrainStones")
{
	TEST_CASE("stone maps decode stones, joints and centres")
	{
		const auto rgba = twoStones();
		const TerrainVisual::StoneMap map(rgba.data(), 128, 128 * 8);
		CHECK(map.tiles() == 1);
		const auto &centres = map.centres(0, 0);
		REQUIRE(centres.size() == 2);
		// Means of texels 0..63 and 64..127 at four texels per logical pixel.
		CHECK(centres[0] == std::array<int, 2>{2048, 4096});
		CHECK(centres[1] == std::array<int, 2>{6144, 4096});
		const auto left = map.at(0, 0, 63 * 64, 4096), right = map.at(0, 0, 64 * 64, 4096);
		CHECK(left.own == 0);
		CHECK(left.across == 1);
		CHECK(right.own == 1);
		CHECK(right.ownTile == 4);
		CHECK(left.joint == 32); // Half a texel: an eighth of a logical pixel.
		CHECK(map.at(0, 0, 0, 0).across == 0);
		// Inconsistent maps are rejected.
		auto bad = rgba;
		bad[0] = 64;
		CHECK_THROWS_AS(TerrainVisual::StoneMap(bad.data(), 128, 1024), std::runtime_error);
		CHECK_THROWS_AS(TerrainVisual::StoneMap(rgba.data(), 64, 1024), std::runtime_error);
	}

	TEST_CASE("applied stones keep or drop whole stones and feather their joint")
	{
		const auto rgba = twoStones();
		const TerrainVisual::StoneMap map(rgba.data(), 128, 1024);
		TerrainVisual::Recipe recipe;
		recipe.width = recipe.height = 8;
		recipe.corners = {1, 2, 1, 2};
		recipe.stones = true;
		recipe.stoneMaterial = 1;
		recipe.stoneGround = 2;
		recipe.stonesKept[4] = 1; // The left stone is kept, the right one dropped.
		const auto weightOf = [](const TerrainVisual::Coverage &c, TerrainVisual::MaterialId id)
		{
			unsigned w = 0;
			for (int i = 0; i < 4; ++i)
				if (c.material[i] == id) w += c.weight[i];
			return w;
		};
		const auto sample = [&](TerrainVisual::Coverage c, int px)
		{
			TerrainVisual::applyStones(recipe, map, px, 4096, c);
			unsigned sum = 0;
			for (auto w : c.weight) sum += w;
			CHECK(sum == 65536);
			return c;
		};
		// The smooth border's split, whichever way it falls, gives way to the stones.
		TerrainVisual::Coverage half;
		half.material = {1, 2, 0, 0};
		half.weight = {32768, 32768, 0, 0};
		TerrainVisual::Coverage path;
		path.material = {1, 0, 0, 0};
		path.weight = {65536, 0, 0, 0};
		TerrainVisual::Coverage ground;
		ground.material = {2, 0, 0, 0};
		ground.weight = {65536, 0, 0, 0};
		for (const auto &c : {half, path, ground})
		{
			CHECK(weightOf(sample(c, 1024), 1) == 65536); // Deep in the kept stone.
			const auto dropped = sample(c, 7168);           // Deep in the dropped one.
			CHECK(weightOf(dropped, 1) == 0);
			CHECK(weightOf(dropped, 2) == 65536);
		}
		// Beside the dropped stone the kept one ends at its face: its joint
		// shows the ground, its face fades in over half a pixel either side.
		const auto joint = sample(half, 63 * 64 + 32), face = sample(half, 58 * 64 + 32),
				   dropped = sample(half, 64 * 64 + 32);
		CHECK(weightOf(joint, 1) < 32768);
		CHECK(weightOf(face, 1) == 65536);
		CHECK(weightOf(dropped, 1) == 0);
		// Seam shading measures from the face: within the joint toward the
		// stone, on the face toward the ground, and on through the dropped stone.
		CHECK(joint.neighbor == 1);
		CHECK(joint.margin == 96); // Three eighths of a pixel inside the joint.
		CHECK(face.neighbor == 2);
		CHECK(face.margin == 224);
		CHECK(dropped.neighbor == 1);
		CHECK(dropped.margin == 32 + 96);
		CHECK(sample(half, 1024).margin == 65535);
	}

	TEST_CASE("laid stone paths draw whole stones along every border [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto &compositor = globals->terrainCompositor();
		const auto &catalog = compositor.catalog();
		const auto brick = catalog.find("brick_path"), grass = catalog.find("grass");
		REQUIRE(!catalog.materials[brick].stones.empty());
		// Sawn planks keep the cut border.
		CHECK(catalog.materials[catalog.find("boardwalk")].stones.empty());
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
		auto &map = fixture.game.map;
		// A diagonal brick path, a plaza with a notch and a bump, and boardwalk.
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
			{
				const int d = x - y;
				if (y >= 2 && y <= 28 && d >= -12 && d < -9)
					map.setVertexTerrain(x, y, BRICK_PATH);
				if (x >= 18 && x <= 28 && y >= 4 && y <= 12 && !(x == 28 && y == 8))
					map.setVertexTerrain(x, y, BRICK_PATH);
				if (x >= 18 && x <= 26 && y >= 18 && y <= 22)
					map.setVertexTerrain(x, y, BOARDWALK);
			}
		map.setVertexTerrain(17, 8, BRICK_PATH);
		SceneMap scene;
		glob2test::observeMap(map, scene);
		const TerrainVisual::StoneMaps maps(catalog);
		const auto *stones = maps.get(brick);
		REQUIRE(stones);
		// Each world stone, by the cell holding its centre and its number there,
		// must be wholly kept or wholly dropped wherever its texels are drawn.
		std::map<std::tuple<int, int, int>, bool> state;
		unsigned conflicts = 0, cells = 0;
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
			{
				const auto recipe = compositor.describe(scene, x, y);
				if (!recipe.stones)
					continue;
				CHECK(recipe.stoneMaterial == brick);
				CHECK(recipe.stoneGround == grass);
				++cells;
				const auto mask = compositor.mask(recipe, 4);
				unsigned slot = 0;
				while (mask.palette[slot] != brick) ++slot;
				for (int ty = 0; ty < 128; ++ty)
					for (int tx = 0; tx < 128; ++tx)
					{
						const int px = tx * 64 + 32, py = ty * 64 + 32;
						const auto texel = stones->at(x & 7, y & 7, px, py);
						if (texel.face < int(TerrainVisual::StoneRimQ8 / 2))
							continue; // Joints and the feathered rim of each face.
						const auto &sample = mask.samples[std::size_t(ty) * 128 + tx];
						const unsigned largest = sample.slots & 3;
						unsigned weight = 65536;
						for (unsigned s = 0, other = 0; s < 4; ++s)
							if (s != largest)
							{
								if (s == slot) weight = sample.others[other];
								else if (largest == slot) weight -= sample.others[other];
								++other;
							}
						CHECK((weight == 0 || weight == 65536));
						const auto key = std::tuple{(x + texel.ownTile % 3 - 1) & 31, (y + texel.ownTile / 3 - 1) & 31,
												  int(texel.own)};
						const bool kept = weight == 65536;
						if (const auto [it, inserted] = state.emplace(key, kept); !inserted && it->second != kept)
							++conflicts;
					}
			}
		CHECK(cells > 0);
		CHECK(conflicts == 0);
		unsigned kept = 0;
		for (const auto &[key, value] : state) kept += value;
		CHECK(kept > 0);
		CHECK(kept < state.size()); // Some border stones were dropped.
		// Boardwalk borders and open ground carry no stone decisions.
		CHECK_FALSE(compositor.describe(scene, 18, 17).stones);
		CHECK_FALSE(compositor.describe(scene, 0, 30).stones);
	}
}
