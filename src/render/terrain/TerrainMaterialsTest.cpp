// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include "ComputeExecutor.h"
#include <PresentationWork.h>
#include <cstring>
#include <numeric>
#include "TerrainMaterials.h"
#include "TerrainCompositor.h"
#include "TerrainCatalogIO.h"
#include "render/SoftwareTerrainCache.h"
#include "render/scene/SceneMap.h"
#include "TerrainCompiledPack.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <filesystem>
#include <SDL3_image/SDL_image.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
TerrainVisual::Catalog catalog()
{
	return TerrainVisual::loadCatalog();
}
// The checkout's catalog as one document, read without the file manager.
nlohmann::json shippedCatalog()
{
	const auto read = [](const std::filesystem::path &path)
	{
		std::ifstream input(glob2test::contentRoot() / path);
		REQUIRE(input);
		return nlohmann::json::parse(input);
	};
	return TerrainVisual::assembleCatalog(read("data/terrain/tileset.json"), [&](const std::string &key)
										  { return read("data/terrain/materials/" + key + ".json"); });
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
	TEST_CASE("connected foliage selects the requested corner mask and keeps legacy decor")
	{
		glob2test::HeadlessGlobals globals;
		const auto c = catalog();
		for (const auto *key : {"hedge", "thicket"})
		{
			const auto id = c.find(key);
			const auto &frames = c.materials[id].decor.connected;
			REQUIRE(frames.size() == 256);
			const auto &interior = c.materials[id].decor.connectedInterior;
			REQUIRE(c.materials[id].decor.connectedCorners);
			REQUIRE(interior.size() == 16 * 7);
			for (unsigned mask = 0; mask < 15; ++mask)
				for (unsigned colours = 0; colours < 16; ++colours)
				{
					const int frame = c.decorFrame(id, 3, 5, true, 73, mask, colours);
					if (!mask) CHECK(frame == -1);
					else CHECK(frame == frames[mask * 16 + colours]);
				}
			// Filled cells pick among the colour's base frame and its interiors.
			for (unsigned colours = 0; colours < 16; ++colours)
			{
				std::set<int> seen;
				for (int y = 0; y < 16; ++y)
					for (int x = 0; x < 16; ++x)
					{
						const int frame = c.decorFrame(id, x, y, false, 73, 15, colours);
						seen.insert(frame);
						const auto first = interior.begin() + colours * 7;
						CHECK((frame == frames[15 * 16 + colours] || std::find(first, first + 7, frame) != first + 7));
					}
				CHECK(seen.size() == 8);
			}
		}
		// Neighbouring cells read the same bit for each shared vertex, including
		// across the toroidal seam, and both colours occur.
		unsigned seen = 0;
		for (unsigned seed : {0u, 73u})
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
				{
					const auto here = TerrainVisual::cornerColours(x, y, 31, 31, seed);
					const auto right = TerrainVisual::cornerColours((x + 1) & 31, y, 31, 31, seed);
					const auto below = TerrainVisual::cornerColours(x, (y + 1) & 31, 31, 31, seed);
					CHECK(((here >> 1) & 5u) == (right & 5u));
					CHECK(((here >> 2) & 3u) == (below & 3u));
					seen |= 1u << here;
				}
		CHECK(seen == 0xffffu);
		const auto id = c.find("boulders");
		for (bool edge : {false, true})
		{
			const auto &frames = edge ? c.materials[id].decor.edge : c.materials[id].decor.full;
			const auto frame = c.decorFrame(id, 3, 7, edge);
			CHECK(std::find(frames.begin(), frames.end(), frame) != frames.end());
		}
	}

	TEST_CASE("tall decor grows upward from its cell; frames up to 64 px stay centred")
	{
		using namespace TerrainVisual;
		// Frames up to 64 px are centred on the 32 px cell, as before.
		CHECK(decorOffsetX(64) == 16);
		CHECK(decorOffsetY(64) == 16);
		CHECK(decorOffsetY(48) == 8);
		CHECK(decorOffsetY(32) == 0);
		// Taller frames keep the 64 px frame's bottom, 16 px below the cell.
		CHECK(decorOffsetY(96) == 48);
		CHECK(decorOffsetY(MaximumDecorFrame) == 80);
		CHECK(MaximumDecorFrame - 32 - decorOffsetY(MaximumDecorFrame) == 16);
		CHECK(decorOffsetX(MaximumDecorFrame) == 48);
		DecorReach reach;
		reach.include(32, 64);
		CHECK((reach.up == 16 && reach.down == 16 && reach.side == 0));
		reach.include(128, 128);
		CHECK((reach.up == 80 && reach.down == 16 && reach.side == 48));
	}
	TEST_CASE("connected foliage rejects incomplete corner groups")
	{
		glob2test::HeadlessGlobals globals;
		auto document = nlohmann::json::parse(catalog().serialized);
		for (auto &material : document["materials"])
			if (material["key"] == "hedge")
			{
				for (unsigned count : {0u, 1u, 15u, 17u, 272u})
				{
					material["decor"]["connected"] = std::vector<int>(count, 0);
					CHECK_THROWS(TerrainVisual::Catalog::parse(document));
				}
			}
	}

	TEST_CASE("fertile ground overlays glint sparsely, out of step and on its own cycle")
	{
		glob2test::HeadlessGlobals globals;
		const auto c = catalog();
		for (const char *key : {"loam", "moss", "spring_meadow"})
			CHECK_FALSE(c.materials[c.find(key)].overlay.cycles.empty());
		for (const char *key : {"grass", "sand", "boulders", "lava"})
			CHECK(c.materials[c.find(key)].overlay.cycles.empty());
		CHECK(c.overlayFrame(c.find("grass"), 3, 4, 100) == -1);
		const auto id = c.find("loam");
		const auto &overlay = c.materials[id].overlay;
		const auto period = std::uint32_t(overlay.ticks * overlay.cycles.front().size());
		unsigned glinting = 0, lit = 0, startsAtZero = 0;
		for (int y = 0; y < 64; ++y)
			for (int x = 0; x < 64; ++x)
			{
				bool cellGlints = false;
				for (std::uint32_t tick = 0; tick < period; tick += std::uint32_t(overlay.ticks))
				{
					const int frame = c.overlayFrame(id, x, y, tick, 9);
					// A step holds for `ticks` scene ticks and the cycle repeats.
					CHECK(c.overlayFrame(id, x, y, tick + std::uint32_t(overlay.ticks) - 1, 9) == frame);
					CHECK(c.overlayFrame(id, x, y, tick + period, 9) == frame);
					if (frame >= 0)
					{
						cellGlints = true;
						++lit;
					}
				}
				glinting += cellGlints;
				startsAtZero += c.overlayFrame(id, x, y, 0, 9) == overlay.cycles.front().front();
			}
		// About density_q8 / 256 of the cells glint, and they do not all flash together.
		const double share = glinting / 4096.0;
		CHECK(std::abs(share - overlay.densityQ8 / 256.0) < 0.05);
		CHECK(lit > glinting);
		CHECK(startsAtZero < glinting / 4);
		// A different map seed picks different cells.
		unsigned moved = 0;
		for (int x = 0; x < 256; ++x)
			for (std::uint32_t tick = 0; tick < period; tick += std::uint32_t(overlay.ticks))
				moved += (c.overlayFrame(id, x, 0, tick, 9) >= 0) != (c.overlayFrame(id, x, 0, tick, 10) >= 0);
		CHECK(moved > 0);
	}

	TEST_CASE("overlays reject empty, all-dark and out-of-range cycles")
	{
		glob2test::HeadlessGlobals globals;
		const auto original = nlohmann::json::parse(catalog().serialized);
		for (const auto &cycles : {nlohmann::json::array(), nlohmann::json::array({nlohmann::json::array()}),
								   nlohmann::json::array({nlohmann::json::array({-1, -1})}),
								   nlohmann::json::array({nlohmann::json::array({-2})}),
								   nlohmann::json::array({nlohmann::json::array({70000})})})
		{
			auto document = original;
			for (auto &material : document["materials"])
				if (material["key"] == "loam")
					material["overlay"]["cycles"] = cycles;
			CHECK_THROWS(TerrainVisual::Catalog::parse(document));
		}
		auto document = original;
		for (auto &material : document["materials"])
			if (material["key"] == "loam")
				material["overlay"]["density_q8"] = 0;
		CHECK_THROWS(TerrainVisual::Catalog::parse(document));
	}

	TEST_CASE("open-edged connected decor is a validated catalog flag")
	{
		glob2test::HeadlessGlobals globals;
		const auto definitions = catalog();
		CHECK(definitions.materials[definitions.find("cliffs")].decor.connectedOpen);
		CHECK(definitions.materials[definitions.find("icefall")].decor.connectedOpen);
		CHECK_FALSE(definitions.materials[definitions.find("hedge")].decor.connectedOpen);
		auto document = nlohmann::json::parse(definitions.serialized);
		for (auto &material : document["materials"])
		{
			if (material["key"] == "cliffs")
			{
				material["decor"]["connected_open"] = 1;
				CHECK_THROWS(TerrainVisual::Catalog::parse(document));
				material["decor"]["connected_open"] = true;
			}
			if (material["key"] == "boulders")
			{
				// Object decor has no corner-mask edges to draw on open ground.
				material["decor"]["connected_open"] = true;
				CHECK_THROWS(TerrainVisual::Catalog::parse(document));
				material["decor"].erase("connected_open");
			}
		}
		CHECK_NOTHROW(TerrainVisual::Catalog::parse(document));
	}

	TEST_CASE("free-standing obstacles keep clear of open-ground masses")
	{
		glob2test::HeadlessGlobals globals;
		const auto definitions = catalog();
		for (const char *key : {"boulders", "ridge_rock", "outcrop", "penitentes", "termite_mounds",
								"hoodoos", "petrified_stumps"})
		{
			CAPTURE(key);
			CHECK(definitions.materials[definitions.find(key)].decor.clearOfMasses == 1);
		}
		// Masses and foliage never use it.
		for (const char *key : {"cliffs", "icefall", "hedge", "thicket"})
		{
			CAPTURE(key);
			CHECK(definitions.materials[definitions.find(key)].decor.clearOfMasses == -1);
		}
		auto document = nlohmann::json::parse(definitions.serialized);
		for (auto &material : document["materials"])
		{
			if (material["key"] == "boulders")
			{
				for (const auto bad : {nlohmann::json(-1), nlohmann::json(TerrainVisual::MaxDecorClearOfMasses + 1),
									   nlohmann::json(true), nlohmann::json("1"), nlohmann::json(1.0)})
				{
					material["decor"]["clear_of_masses"] = bad;
					CHECK_THROWS(TerrainVisual::Catalog::parse(document));
				}
				material["decor"]["clear_of_masses"] = 0;
				CHECK(TerrainVisual::Catalog::parse(document)
						  .materials[definitions.find("boulders")].decor.clearOfMasses == 0);
				material["decor"]["clear_of_masses"] = 1;
			}
			// Masses and connected foliage tile across cells, so skipping one
			// would cut their surface.
			if (material["key"] == "cliffs" || material["key"] == "hedge")
			{
				material["decor"]["clear_of_masses"] = 1;
				CHECK_THROWS(TerrainVisual::Catalog::parse(document));
				material["decor"].erase("clear_of_masses");
			}
		}
		CHECK_NOTHROW(TerrainVisual::Catalog::parse(document));
	}

	TEST_CASE("connected canopy ground leaves contact shadows to the foliage [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		const auto c = catalog();
		const auto grass = c.find("grass"), hedge = c.find("hedge");
		auto unshadowed = c, legacy = c;
		unshadowed.materials[hedge].seam.cast = 0;
		legacy.materials[hedge].decor.connected.clear();
		TerrainVisual::Compositor canopy(c), ground(unshadowed), oldGround(legacy);
		canopy.prepare(false, 0); ground.prepare(false, 0); oldGround.prepare(false, 0);
		TerrainVisual::Recipe r;
		r.corners = {grass, hedge, grass, hedge}; r.width = r.height = 16;
		GAGCore::DrawableSurface actual(32, 32), expected(32, 32), old(32, 32);
		canopy.compose(r, actual.getSDLSurface(), 0, 0, 1);
		ground.compose(r, expected.getSDLSurface(), 0, 0, 1);
		oldGround.compose(r, old.getSDLSurface(), 0, 0, 1);
		unsigned legacyDifferences = 0;
		for (int y = 0; y < 32; ++y)
		{
			const auto row = [y](SDL_Surface *surface) {
				return reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch);
			};
			for (int x = 0; x < 32; ++x)
			{
				CHECK(row(actual.getSDLSurface())[x] == row(expected.getSDLSurface())[x]);
				legacyDifferences += row(actual.getSDLSurface())[x] != row(old.getSDLSurface())[x];
			}
		}
		CHECK(legacyDifferences > 0);
	}

	TEST_CASE("contextual borders preserve seams and normalization across both wrapped axes")
	{
		glob2test::HeadlessGlobals globals;
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
		glob2test::HeadlessGlobals globals;
		// Contextual geometry is the version 1-3 path; version 4 draws every
		// contact with the smooth field (TerrainContinuity covers it).
		auto c = catalog(); c.boundaryWarp = {}; c.version = 3;
		// Both profiles opt in to contextual geometry under the version 3 rule.
		const auto a = c.find("sand"), b = c.find("grass");
		auto additional = c.materials[c.find("grass")];
		additional.key = "fixture-natural";
		c.materials.push_back(additional); // Sharp borders also remain sharp against new materials.
		for (const auto &material : c.materials)
			for (auto sharp : {"road", "boardwalk", "lava", "ember_field", "void_hole", "chasm"})
				CHECK_FALSE(c.contextualFor(c.find(sharp), c.find(material.key)));
		auto old = c;
		for (auto &profile : old.profiles) profile.shape = TerrainVisual::BoundaryShape::Patch;
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
			std::array<TerrainVisual::MaterialId, 4>{c.find("boardwalk"), a, c.find("boardwalk"), a}})
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
		glob2test::HeadlessGlobals globals;
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
		glob2test::HeadlessGlobals globals;
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
		glob2test::HeadlessGlobals globals;
		// Contextual geometry is the version 1-3 path; version 4 draws every
		// contact with the smooth field (TerrainContinuity covers it).
		auto c = catalog(); c.boundaryWarp = {}; c.version = 3;
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

	TEST_CASE("halo edits invalidate adjacent terrain pages including wrap and undo [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = SAND, .discovered = true});
		fixture.game.map.setVertexTerrain(15, 8, SAND);
		fixture.game.map.setVertexTerrain(16, 8, WATER);
		fixture.game.map.setVertexTerrain(0, 8, SAND);
		fixture.game.map.setVertexTerrain(1, 8, WATER);
		SceneMap scene;
		SoftwareTerrainCache cache;
		const auto prepare = [&]
		{
			glob2test::observeMap(fixture.game.map, scene);
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0, fixture.team->me, true, 0));
			std::vector<Uint32> expected;
			for (bool cached : {false, true})
			{
				globals->gfx->drawFilledRect(0, 0, 640, 480, GAGCore::Color(11, 22, 33));
				if (cached) cache.draw(*globals->gfx);
				else SoftwareTerrainCache::drawUncached(scene, *globals->terrain,
					0, 0, 15, 15, 0, 0, fixture.team->me, true, 0);
				const auto *surface = globals->gfx->getSDLSurface();
				std::vector<Uint32> pixels;
				for (int y = 0; y < surface->h; ++y)
				{
					const auto *row = reinterpret_cast<const Uint32 *>(
						static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch);
					pixels.insert(pixels.end(), row, row + surface->w);
				}
				if (cached) CHECK(pixels == expected);
				else expected = std::move(pixels);
			}
		};
		prepare();
		for (int x : {17, 63})
		{
			const auto original = fixture.game.map.vertexTerrainAt(x, 8);
			const auto before = cache.cacheRebuilds();
			fixture.game.map.setVertexTerrain(x, 8, original == WATER ? SAND : WATER);
			prepare(); CHECK(cache.cacheRebuilds() == before + 1);
			fixture.game.map.setVertexTerrain(x, 8, original);
			prepare(); CHECK(cache.cacheRebuilds() == before + 2);
			prepare(); CHECK(cache.cacheRebuilds() == before + 2);
			fixture.game.map.setVertexTerrain(x, 8, original == WATER ? SAND : WATER);
			prepare(); CHECK(cache.cacheRebuilds() == before + 3); // Redo the same edit.
			fixture.game.map.setVertexTerrain(x, 8, original);
			prepare(); CHECK(cache.cacheRebuilds() == before + 4);
		}
	}

	TEST_CASE("shore calm edits beyond the border halo invalidate the calmed page [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = WATER, .discovered = true});
		// Page zero ends at vertex 16; land at vertex 18 still calms its water.
		constexpr int land = SoftwareTerrainCache::ChunkTiles + TerrainVisual::ShoreCalmSearch;
		REQUIRE(TerrainVisual::ShoreCalmSearch >= 2);
		REQUIRE(SoftwareTerrainCache::SourceHalo >= TerrainVisual::ShoreCalmSearch);
		fixture.game.map.setVertexTerrain(land, 8, SAND);
		SceneMap scene;
		SoftwareTerrainCache cache;
		const auto prepare = [&]
		{
			glob2test::observeMap(fixture.game.map, scene);
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0, fixture.team->me, true, 0));
			std::vector<Uint32> expected;
			for (bool cached : {false, true})
			{
				globals->gfx->drawFilledRect(0, 0, 640, 480, GAGCore::Color(11, 22, 33));
				if (cached) cache.draw(*globals->gfx);
				else SoftwareTerrainCache::drawUncached(scene, *globals->terrain,
					0, 0, 15, 15, 0, 0, fixture.team->me, true, 0);
				const auto *surface = globals->gfx->getSDLSurface();
				std::vector<Uint32> pixels;
				for (int y = 0; y < surface->h; ++y)
				{
					const auto *row = reinterpret_cast<const Uint32 *>(
						static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch);
					pixels.insert(pixels.end(), row, row + surface->w);
				}
				if (cached) CHECK(pixels == expected);
				else expected = std::move(pixels);
			}
		};
		prepare();
		const auto before = cache.cacheRebuilds();
		fixture.game.map.setVertexTerrain(land, 8, WATER);
		prepare(); CHECK(cache.cacheRebuilds() == before + 1);
		fixture.game.map.setVertexTerrain(land, 8, SAND);
		prepare(); CHECK(cache.cacheRebuilds() == before + 2);
	}

	TEST_CASE("natural border visual fixture and cold composition timings [display] [artifacts]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 768, .height = 768});
		auto current = catalog(); current.compiledPack.clear();
		auto previous = current;
		for (auto &profile : previous.profiles) profile.shape = TerrainVisual::BoundaryShape::Patch;
		const auto water = current.find("water"), sand = current.find("sand"), grass = current.find("grass");
		const auto field = [&](int scenario, int x, int y) -> TerrainVisual::MaterialId
		{
			const int dx = x - 4, dy = y - 4;
			switch (scenario)
			{
			case 0: return x + y < 8 ? sand : water; // diagonal
			case 1: return x < 2 || dx*dx + dy*dy < 10 ? sand : water; // headland
			case 2: return x > 5 || dx*dx + dy*dy < 10 ? water : sand; // bay
			case 3: return std::abs(x - (3 + int(std::round(2 * std::sin(y * .7))))) <= 0 ? water : sand;
			case 4: return x == 3 ? water : sand; // channel
			case 5: return x == 3 ? sand : water; // land strip
			case 6: return x == 3 && y == 3 ? sand : water; // island
			case 7: return (x + y) & 1 ? sand : water; // ambiguous diagonal
			default: return x < 4 ? water : y < 4 ? sand : grass; // three materials
			}
		};
		std::vector<TerrainVisual::Recipe> recipes;
		for (int scenario = 0; scenario < 9; ++scenario)
			for (int y = 0; y < 8; ++y)
				for (int x = 0; x < 8; ++x)
					recipes.push_back(contextualRecipe(x, y, 8,
						[&](int x, int y) { return field(scenario, x, y); }));
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .discovered = true});
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
			{
				const auto material = x < 24 && y < 24 ? field((y / 8) * 3 + x / 8, x % 8, y % 8) : grass;
				fixture.game.map.setVertexTerrain(x, y, material == water ? WATER : material == sand ? SAND : GRASS);
			}
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		const auto simulationChecksum = fixture.game.checkSum(nullptr, nullptr, nullptr, true);
		std::ofstream log(glob2test::artifactDir() / "composition.txt");
		log << "Panels row-major: diagonal, headland, bay, S gully, water channel, land strip, island, checkerboard, three-material junction.\n";
		log << "Before disables contextual profiles; textures, seeds and source vertices are identical.\n";
		for (bool after : {false, true})
		{
			TerrainVisual::Compositor compositor(after ? current : previous);
			for (int scale : {1, 4})
			{
				compositor.prepare(scale > 1, 0);
				GAGCore::DrawableSurface image(768 * scale, 768 * scale);
				std::array<double, 5> timings{};
				for (auto &timing : timings)
				{
					const auto start = std::chrono::steady_clock::now();
					for (unsigned i = 0; i < recipes.size(); ++i)
					{
						const unsigned scenario = i / 64;
						compositor.compose(recipes[i], image.getSDLSurface(),
							((scenario % 3) * 256 + (i % 8) * 32) * scale,
							((scenario / 3) * 256 + ((i % 64) / 8) * 32) * scale, scale);
					}
					timing = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				}
				std::sort(timings.begin(), timings.end());
				const auto ms = timings[timings.size() / 2];
				const std::string name = after ? "after" : "before";
				log << name << " scale=" << scale << " cold_ms=" << ms << " median_of=5" << '\n';
				CHECK(IMG_SavePNG(image.getSDLSurface(),
					(glob2test::artifactDir() / (name + "-" + std::to_string(scale) + ".png")).string().c_str()));
				if (scale == 1)
				{
					GAGCore::DrawableSurface zoom(384, 384);
					REQUIRE(SDL_BlitSurfaceScaled(image.getSDLSurface(), nullptr, zoom.getSDLSurface(), nullptr, SDL_SCALEMODE_LINEAR));
					CHECK(IMG_SavePNG(zoom.getSDLSurface(),
						(glob2test::artifactDir() / (name + "-zoom-half.png")).string().c_str()));
				}
			}
			globals->terrainCompositor_ = std::make_unique<TerrainVisual::Compositor>(after ? current : previous);
			SoftwareTerrainCache cache;
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 23, 23, 0, 0, fixture.team->me, true, 0));
			const auto rebuilds = cache.cacheRebuilds();
			std::array<double, 5> warmTimings{};
			for (auto &timing : warmTimings)
			{
				const auto start = std::chrono::steady_clock::now();
				for (int frame = 0; frame < 100; ++frame)
				{
					REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 23, 23, 0, 0, fixture.team->me, true, 0));
					cache.draw(*globals->gfx);
				}
				timing = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 100;
			}
			CHECK(cache.cacheRebuilds() == rebuilds);
			std::sort(warmTimings.begin(), warmTimings.end());
			log << (after ? "after" : "before") << " warm_frame_ms=" << warmTimings[2] << " median_of=5x100 frames\n";
		}
		CHECK(fixture.game.checkSum(nullptr, nullptr, nullptr, true) == simulationChecksum);
		log << "simulation_checksum=" << simulationChecksum << " unchanged after both presentations\n";
	}

	TEST_CASE("border gallery renders each kind of material contact [display] [artifacts]")
	{
		// Review fixture for boundary profiles: twelve 8x8-cell panels, each a
		// material pair (or a junction) painted with the same island, diagonal
		// shore, one-cell strip, bend and lone vertex.
		glob2test::HeadlessGlobals globals({.display = true, .width = 1024, .height = 768});
		auto c = catalog(); c.compiledPack.clear();
		struct Panel { const char *a, *b, *c; };
		const Panel panels[] = {
			{"water", "deep_water", nullptr}, {"deep_water", "dark_water", nullptr},
			{"water", "deep_water", "grass"}, {"water", "grass", nullptr},
			{"water", "sand", nullptr}, {"ice", "water", nullptr},
			{"lava", "grass", nullptr}, {"lava", "boulders", nullptr},
			{"road", "grass", nullptr}, {"road", "boulders", nullptr},
			{"brick_path", "heath", nullptr}, {"boardwalk", "water", nullptr}};
		const auto shape = [](int x, int y)
		{
			const float dx = x - 2.5f, dy = y - 2.5f;
			return dx * dx + dy * dy < 5.5f || x + y >= 11 || (x == 6 && y <= 3) || (x == 1 && y == 6);
		};
		std::ofstream log(glob2test::artifactDir() / "border-gallery.txt");
		log << "Panels row-major (second material inside the shapes):\n";
		for (const auto &panel : panels)
			log << "  " << panel.a << " / " << panel.b << (panel.c ? std::string(" / ") + panel.c + " (top-right junction)" : "") << '\n';
		TerrainVisual::Compositor compositor(c);
		for (int scale : {1, 4})
		{
			compositor.prepare(scale > 1, 0);
			GAGCore::DrawableSurface image(1024 * scale, 768 * scale);
			for (unsigned index = 0; index < std::size(panels); ++index)
			{
				const auto &panel = panels[index];
				const auto a = c.find(panel.a), b = c.find(panel.b);
				const auto third = panel.c ? c.find(panel.c) : a;
				for (int y = 0; y < 8; ++y)
					for (int x = 0; x < 8; ++x)
					{
						const auto recipe = contextualRecipe(x, y, 8, [&](int x, int y)
							{ return panel.c && x >= 5 && y <= 2 ? third : shape(x, y) ? b : a; });
						compositor.compose(recipe, image.getSDLSurface(),
							((index % 4) * 256 + x * 32) * scale, ((index / 4) * 256 + y * 32) * scale, scale);
					}
			}
			CHECK(IMG_SavePNG(image.getSDLSurface(),
				(glob2test::artifactDir() / ("border-gallery-" + std::to_string(scale) + ".png")).string().c_str()));
		}
	}

	TEST_CASE("terrain crossfade preserves endpoints midpoint loop and paused revisions [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto definitions = catalog();
		definitions.compiledPack.clear();
		const auto id = definitions.find("ice");
		auto &ice = definitions.materials[id];
		ice.sprite = "data/gfx/terrain";
		ice.variantGrid = 0; ice.variantBlocks = 1;
		ice.variants = {{272, 1}};
		ice.totalWeight = 1;
		ice.animationFrames = 2;
		ice.animationStride = 1;
		ice.animationTicks = 4;
		ice.periodicEdges = true;
		struct RestoreFrame
		{
			GAGCore::DrawableSurface *frame;
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> original;
			~RestoreFrame()
			{
				if (!original) return;
				SDL_SetSurfaceBlendMode(original.get(), SDL_BLENDMODE_NONE);
				SDL_BlitSurface(original.get(), nullptr, frame->getSDLSurface(), nullptr);
				frame->markPixelsChanged();
			}
		};
		auto *a = globals->terrain->nativeFrame(272), *b = globals->terrain->nativeFrame(273);
		RestoreFrame restoreA{a, {SDL_DuplicateSurface(a->getSDLSurface()), SDL_DestroySurface}};
		RestoreFrame restoreB{b, {SDL_DuplicateSurface(b->getSDLSurface()), SDL_DestroySurface}};
		REQUIRE(restoreA.original);
		REQUIRE(restoreB.original);
		a->drawFilledRect(0, 0, 32, 32, 240, 0, 0);
		b->drawFilledRect(0, 0, 32, 32, 0, 0, 240);
		TerrainVisual::Compositor compositor(definitions);
		TerrainVisual::Recipe recipe;
		recipe.width = recipe.height = 16;
		recipe.corners.fill(id);
		GAGCore::DrawableSurface result(32, 32);
		const auto pixel = [&](int time)
		{
			compositor.prepare(false, time);
			compositor.compose(recipe, result.getSDLSurface(), 0, 0, 1);
			return static_cast<const Uint32 *>(result.getSDLSurface()->pixels)[0];
		};
		CHECK(pixel(0) == 0xfff00000u);
		CHECK(pixel(2) == 0xff780078u);
		const auto revision = compositor.textureRevision(id);
		const auto bytes = compositor.sourceBytes();
		CHECK(pixel(2) == 0xff780078u);
		CHECK(compositor.textureRevision(id) == revision);
		CHECK(pixel(4) == 0xff0000f0u);
		CHECK(pixel(6) == 0xff780078u);
		CHECK(pixel(8) == 0xfff00000u);
		CHECK(compositor.sourceBytes() == bytes);
		// Jump by a whole phase with the same blend fraction: swapped endpoints
		// must still invalidate composed terrain pages.
		const auto beforeJump = compositor.textureRevision(id);
		CHECK(pixel(12) == 0xff0000f0u);
		CHECK(compositor.textureRevision(id) == beforeJump + 1);
		b->drawFilledRect(0, 0, 32, 32, 0, 240, 0);
		CHECK(pixel(12) == 0xff00f000u);
		REQUIRE(SDL_FillSurfaceRect(b->getSDLSurface(), nullptr, 0x0000ffffu));
		b->markPixelsChanged();
		CHECK(pixel(2) == 0x80f00000u); // Transparent cyan contributes no colour.
	}
	TEST_CASE("stepped crossfades hold each weight and its revision for several ticks [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto definitions = catalog();
		definitions.compiledPack.clear();
		const auto id = definitions.find("ice");
		auto &ice = definitions.materials[id];
		ice.sprite = "data/gfx/terrain";
		ice.variantGrid = 0; ice.variantBlocks = 1;
		ice.variants = {{272, 1}};
		ice.totalWeight = 1;
		ice.animationFrames = 2;
		ice.animationStride = 1;
		ice.animationTicks = 8;
		ice.animationBlendSteps = 2;
		ice.periodicEdges = true;
		auto *a = globals->terrain->nativeFrame(272), *b = globals->terrain->nativeFrame(273);
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> originalA(
			SDL_DuplicateSurface(a->getSDLSurface()), SDL_DestroySurface),
			originalB(SDL_DuplicateSurface(b->getSDLSurface()), SDL_DestroySurface);
		REQUIRE(originalA);
		REQUIRE(originalB);
		a->drawFilledRect(0, 0, 32, 32, 240, 0, 0);
		b->drawFilledRect(0, 0, 32, 32, 0, 0, 240);
		{
			TerrainVisual::Compositor compositor(definitions);
			TerrainVisual::Recipe recipe;
			recipe.width = recipe.height = 16;
			recipe.corners.fill(id);
			GAGCore::DrawableSurface result(32, 32);
			const auto pixel = [&](int time)
			{
				compositor.prepare(false, time);
				compositor.compose(recipe, result.getSDLSurface(), 0, 0, 1);
				return static_cast<const Uint32 *>(result.getSDLSurface()->pixels)[0];
			};
			// Two steps per eight-tick phase: the endpoint, then the midpoint.
			std::vector<std::uint64_t> revisions;
			for (int time = 0; time < 17; ++time)
			{
				const Uint32 expected[] = {0xfff00000u, 0xff780078u, 0xff0000f0u, 0xff780078u};
				CHECK(pixel(time) == expected[time / 4 % 4]);
				revisions.push_back(compositor.textureRevision(id));
			}
			for (int time = 1; time < 17; ++time)
				CHECK(revisions[time] == revisions[time - 1] + (time % 4 == 0));
		}
		for (auto [frame, original] : {std::pair{a, originalA.get()}, std::pair{b, originalB.get()}})
		{
			SDL_SetSurfaceBlendMode(original, SDL_BLENDMODE_NONE);
			SDL_BlitSurface(original, nullptr, frame->getSDLSurface(), nullptr);
			frame->markPixelsChanged();
		}
	}
	TEST_CASE("animation_blend_steps is optional and bounded by the phase length")
	{
		const auto j = shippedCatalog();
		std::size_t lava = 0, grass = 0;
		while (j["materials"][lava]["key"] != "lava") ++lava;
		while (j["materials"][grass]["key"] != "grass") ++grass;
		const auto parsed = TerrainVisual::Catalog::parse(j);
		// Shipped lava crossfades in four steps per phase; without the field it blends every tick.
		CHECK(parsed.materials[parsed.find("lava")].animationBlendSteps == 4);
		auto absent = j;
		absent["materials"][lava].erase("animation_blend_steps");
		const auto every = TerrainVisual::Catalog::parse(absent);
		CHECK(every.materials[every.find("lava")].animationBlendSteps == 0);
		const int ticks = j["materials"][lava]["animation_ticks"];
		for (int steps : {1, ticks / 2, ticks})
		{
			auto valid = j;
			valid["materials"][lava]["animation_blend_steps"] = steps;
			const auto c = TerrainVisual::Catalog::parse(valid);
			CHECK(c.materials[c.find("lava")].animationBlendSteps == steps);
		}
		for (const nlohmann::json steps : {nlohmann::json(0), nlohmann::json(ticks + 1), nlohmann::json("4")})
		{
			auto invalid = j;
			invalid["materials"][lava]["animation_blend_steps"] = steps;
			CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		}
		auto still = j;
		still["materials"][grass]["animation_blend_steps"] = 1; // Nothing to crossfade.
		CHECK_THROWS(TerrainVisual::Catalog::parse(still));
	}
	TEST_CASE("streamed animated HD keeps later phases resident across advances [display]")
	{
		const auto directory = glob2test::artifactDir() / "animated-hd";
		std::filesystem::create_directories(directory);
		std::filesystem::copy_file(glob2test::sourceRoot() / "test/fixtures/image-assets/terrain-hd-solid.webp",
			directory / "frame.webp", std::filesystem::copy_options::overwrite_existing);
		{
			std::ofstream frames(directory / "frames.txt");
			frames << "GLOB2_HIGHRES 1\n";
			for (int frame = 0; frame < 256; ++frame)
				frames << "terrain-grass" << frame << " 32 32 4 frame.webp -\n";
		}
		glob2test::ScopedEnvironment hdPath("GLOB2_EXPERIMENT_TEXTURE_DIR", directory.string().c_str());
		glob2test::HeadlessGlobals globals({.display = true, .width = 256, .height = 256,
			.screenFlags = Uint32(GAGCore::GraphicContext::PORTABLEGPU)});
		auto definitions = catalog();
		definitions.compiledPack.clear();
		const auto id = definitions.find("grass");
		auto &grass = definitions.materials[id];
		// Four phases borrow the bank's four layouts, one streamed HD frame each.
		grass.variants.resize(64);
		grass.totalWeight = 64;
		grass.variantBlocks = 1;
		grass.animationFrames = 4;
		grass.animationStride = 64;
		grass.animationTicks = 2;
		GAGCore::Sprite::deferHighResolution(grass.sprite);
		GAGCore::Sprite::setHighResolution(true);
		struct Restore { ~Restore() { GAGCore::Sprite::setHighResolution(false); } } restore;
		TerrainVisual::Compositor compositor(definitions);
		std::vector<TerrainVisual::Recipe> recipes;
		for (int y = 0; y < 2; ++y)
			for (int x = 0; x < 2; ++x)
			{
				TerrainVisual::Recipe r;
				r.x = x; r.y = y;
				r.width = r.height = 16;
				r.corners.fill(id);
				recipes.push_back(r);
			}
		compositor.prepareForRecipes(true, 0, recipes);
		GAGCore::Sprite::finishHighResolution();
		compositor.prepareForRecipes(true, 0, recipes);
		auto *sprite = GAGCore::Toolkit::getSprite(grass.sprite);
		for (int phase = 0; phase < 4; ++phase)
			for (int cell : {0, 1, 8, 9})
				CHECK(sprite->baseFrame(cell + 64 * phase)->getW() == 128);
		CHECK(compositor.scale() == 4);
		// Cycle twice through every phase without streaming time: each advance
		// reuses resident frames, so no preparation falls back to native art.
		const auto before = compositor.highResolutionCoverage();
		for (int time = 1; time < 17; ++time)
			compositor.prepareForRecipes(true, time, recipes);
		const auto after = compositor.highResolutionCoverage();
		CHECK(after.prepares == before.prepares + 16);
		CHECK(after.fallbacks == before.fallbacks);
	}
	TEST_CASE("animation invalidates only pages using that material [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto definitions = catalog();
		definitions.compiledPack.clear(); // Synthetic animation has no compiled artifact.
		auto &ice = definitions.materials[definitions.find("ice")];
		ice.sprite = "data/gfx/terrain";
		ice.variantGrid = 0; ice.variantBlocks = 1;
		ice.variants = {{272, 1}};
		ice.totalWeight = 1;
		ice.animationFrames = 2;
		ice.animationStride = 1;
		ice.animationTicks = 4; // Exercise a crossfade within a phase.
		globals->terrainCompositor_ =
			std::make_unique<TerrainVisual::Compositor>(std::move(definitions));
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .discovered = true});
		for (int y = 0; y < 32; ++y)
			for (int x = 32; x < 48; ++x)
				fixture.game.map.paintCell(x, y, ICE);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map,scene);
		SoftwareTerrainCache cache;
		const auto prepare = [&](int x, int time)
		{
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, x, 0, fixture.team->me,
								  true, time));
		};
		prepare(0, 0);
		const auto grassRebuilds = cache.cacheRebuilds();
		prepare(0, 1);
		CHECK(cache.cacheRebuilds() == grassRebuilds);
		prepare(32, 1);
		const auto iceRebuilds = cache.cacheRebuilds();
		prepare(32, 2);
		CHECK(cache.cacheRebuilds() == iceRebuilds + 1);
		prepare(0, 2);
		CHECK(cache.cacheRebuilds() == iceRebuilds + 1);
	}
	TEST_CASE("surf steps rebuild shoreline pages but keep static open water resident [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .terrain = DARK_WATER, .discovered = true});
		for (int y = 0; y < 32; ++y)
			for (int x = 40; x < 48; ++x)
				fixture.game.map.setVertexTerrain(x, y, SAND);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		SoftwareTerrainCache cache;
		const auto prepare = [&](int x, int time) {
			REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, x, 0,
			                      fixture.team->me, true, time));
		};
		auto &compositor = globals->terrainCompositor(scene.frozenAssetBundle());
		const int step = compositor.catalog().materials[compositor.catalog().find("dark_water")].surf.ticks;
		prepare(0, 0);
		prepare(32, 0);
		const auto before = cache.cacheRebuilds();
		prepare(0, step);
		CHECK(cache.cacheRebuilds() == before);
		prepare(32, step);
		CHECK(cache.cacheRebuilds() == before + 1);
	}

	TEST_CASE("unequal variant weights affect deterministic texture selection")
	{
		glob2test::HeadlessGlobals globals;
		auto definitions = catalog();
		const auto id = definitions.find("ice");
		auto &material = definitions.materials[id];
		material.sprite = "data/gfx/terrain";
		material.variantGrid = 0; material.variantBlocks = 1;
		material.variants = {{272, 1}, {273, 9}};
		material.totalWeight = 10;
		unsigned frequent = 0;
		for (int y = 0; y < 100; ++y)
			for (int x = 0; x < 100; ++x)
			{
				const auto index = definitions.variantIndex(id, x, y);
				REQUIRE(index < 2);
				CHECK(definitions.frame(id, x, y, 0) == material.variants[index].frame);
				frequent += index == 1;
			}
		CHECK(frequent > 8800);
		CHECK(frequent < 9200);
	}

	TEST_CASE("compiled pages validate metadata and stale catalogs use source fallback [artifacts]")
	{
		glob2test::HeadlessGlobals globals;
		auto j = shippedCatalog();
		j["compiled_pack"] = "data/terrain/test-pack/atlas.json";
		const auto root = glob2test::artifactDir();
		const auto dir = root / "data/terrain/test-pack";
		std::filesystem::create_directories(dir);
		globals->fileManager->addDir(root.string());
		auto c = TerrainVisual::Catalog::parse(j);
		std::filesystem::copy_file(
			glob2test::sourceRoot() / "test/fixtures/image-assets/terrain-hd-solid.webp",
			dir / "page.webp", std::filesystem::copy_options::overwrite_existing);
		nlohmann::json packed = {
			{"version", 1},
			{"catalog", j},
			{"pages", {{{"size", {128, 128}}, {"levels", {"page.webp"}}}}},
			{"frames", {{{"source", "fixture.png"}, {"page", 0}, {"rect", {4, 4, 32, 32}}}}}};
		const auto write = [&]
		{
			std::ofstream out(dir / "atlas.json");
			out << packed;
		};
		write();
		auto pack = TerrainVisual::CompiledPack::load(c);
		REQUIRE(pack);
		std::vector<std::array<unsigned char, 4>> read;
		pack->read("fixture.png", read);
		REQUIRE(read.size() == 1024);
		CHECK(read[0] == std::array<unsigned char, 4>{23, 57, 91, 255});
		CHECK(pack->bytes() == 128 * 128 * 4);
		GAGCore::DrawableSurface native(32, 32);
		native.drawFilledRect(0, 0, 32, 32, GAGCore::Color(23, 57, 91));
		// An old pack without decoded-source provenance is safe to read but must
		// not override source artwork. A current pack accepts only matching pixels.
		CHECK_FALSE(pack->matches("fixture.png", native.getSDLSurface()));
		packed["frames"][0]["native_rgba_fnv1a64"] = "422c101433645325";
		write();
		pack = TerrainVisual::CompiledPack::load(c);
		CHECK(pack->matches("fixture.png", native.getSDLSurface()));
		CHECK_FALSE(pack->matches("unknown.png", native.getSDLSurface()));
		// Construct the override before loading the pack, reproducing startup with
		// edited/overridden artwork rather than just a later content revision.
		native.drawFilledRect(0, 0, 1, 1, GAGCore::Color(24, 57, 91));
		pack = TerrainVisual::CompiledPack::load(c);
		CHECK_FALSE(pack->matches("fixture.png", native.getSDLSurface()));
		packed["frames"][0]["native_rgba_fnv1a64"] = "not-a-fingerprint";
		write();
		CHECK_THROWS(TerrainVisual::CompiledPack::load(c));
		packed["frames"][0]["native_rgba_fnv1a64"] = "422c101433645325";
		write();
		auto edited = j;
		edited["materials"][0]["variants"][0]["weight"] = 2;
		CHECK_FALSE(TerrainVisual::CompiledPack::load(TerrainVisual::Catalog::parse(edited)));
		packed["frames"][0]["rect"] = {124, 4, 32, 32};
		write();
		CHECK_THROWS(TerrainVisual::CompiledPack::load(c));
		std::filesystem::remove(dir / "atlas.json");
		CHECK_FALSE(TerrainVisual::CompiledPack::load(c));
	}
	TEST_CASE("compositor preserves native artwork overridden before startup [display] [artifacts]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto document = shippedCatalog();
		document["compiled_pack"] = "data/terrain/startup-override/atlas.json";
		for (auto &material : document["materials"])
			if (material["key"] == "ice")
			{
				material["sprite"] = "data/gfx/terrain";
				material.erase("variant_grid"); material.erase("variant_blocks"); material.erase("variant_block_group");
				material.erase("stones"); // A stone map spans a variant block.
				material["variants"] = {{{"frame", 272}, {"weight", 1}}};
			}
		const auto directory = glob2test::artifactDir() / "data/terrain/startup-override";
		std::filesystem::create_directories(directory);
		globals->fileManager->addDir(glob2test::artifactDir().string());
		std::filesystem::copy_file(
			glob2test::sourceRoot() / "test/fixtures/image-assets/terrain-hd-solid.webp",
			directory / "page.webp", std::filesystem::copy_options::overwrite_existing);
		const nlohmann::json packed = {
			{"version", 1},
			{"catalog", document},
			{"pages", {{{"size", {128, 128}}, {"levels", {"page.webp"}}}}},
			{"frames",
			 {{{"source", "data/gfx/terrain272.png"},
			   {"page", 0},
			   {"rect", {4, 4, 32, 32}},
			   {"native_rgba_fnv1a64", "422c101433645325"}}}}};
		{
			std::ofstream output(directory / "atlas.json");
			output << packed;
		}
		auto *native = globals->terrain->nativeFrame(272);
		struct RestoreNativeFrame
		{
			GAGCore::DrawableSurface *frame;
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> original;
			~RestoreNativeFrame()
			{
				if (original)
				{
					SDL_SetSurfaceBlendMode(original.get(), SDL_BLENDMODE_NONE);
					SDL_BlitSurface(original.get(), nullptr, frame->getSDLSurface(), nullptr);
					frame->markPixelsChanged();
				}
			}
		} restore{native, {SDL_DuplicateSurface(native->getSDLSurface()), SDL_DestroySurface}};
		REQUIRE(restore.original);
		native->drawFilledRect(0, 0, 32, 32, 23, 57, 91);
		const auto definitions = TerrainVisual::Catalog::parse(document);
		const auto pack = TerrainVisual::CompiledPack::load(definitions);
		REQUIRE(pack);
		REQUIRE(pack->matches("data/gfx/terrain272.png", native->getSDLSurface()));

		// Startup revision tracking sees this surface as clean. Only comparison
		// with the exported pixels prevents the stale pack from replacing it.
		native->drawFilledRect(0, 0, 32, 32, 201, 23, 189);
		TerrainVisual::Compositor compositor(definitions);
		compositor.prepare(false, 0);
		TerrainVisual::Recipe recipe;
		recipe.width = recipe.height = 16;
		recipe.corners.fill(definitions.find("ice"));
		GAGCore::DrawableSurface result(32, 32);
		auto *surface = result.getSDLSurface();
		compositor.compose(recipe, surface, 0, 0, 1);
		for (int y = 0; y < 32; ++y)
		{
			const auto *row = reinterpret_cast<const Uint32 *>(
				static_cast<const unsigned char *>(surface->pixels) + y * surface->pitch);
			for (int x = 0; x < 32; ++x)
				CHECK(row[x] == 0xffc917bdu);
		}
	}
	TEST_CASE("an additional material and a 64-material catalog compose without renderer branches "
			  "[display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto j = shippedCatalog();
		// Pad the shipped catalog to 64 materials with grass clones.
		for (auto i = j["materials"].size(); i < 64; ++i)
		{
			auto m = j["materials"][2];
			m["key"] = "fixture-" + std::to_string(i);
			j["materials"].push_back(m);
		}
		REQUIRE(j["materials"].size() == 64);
		TerrainVisual::Compositor compositor(TerrainVisual::Catalog::parse(j));
		compositor.prepare(false, 0);
		TerrainVisual::Recipe r;
		r.width = r.height = 16;
		for (int i = 0; i < 4; ++i)
			r.corners[i] = 60 + i;
		GAGCore::DrawableSurface result(32, 32);
		compositor.compose(r, result.getSDLSurface(), 0, 0, 1);
		CHECK(result.hasOpaquePixels());
	}

	TEST_CASE("seam treatment tones only the lower material beside a higher edge [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto definitions = catalog();
		definitions.compiledPack.clear();
		definitions.boundaryWarp = {}; // A straight ice/grass seam at x = 16.
		for (auto &profile : definitions.profiles)
			profile.roughness = profile.speckle = 0;
		for (auto &material : definitions.materials)
			material.seam = {};
		const auto ice = definitions.find("ice"), grass = definitions.find("grass");
		TerrainVisual::Recipe r;
		r.width = r.height = 16;
		r.x = 3;
		r.y = 5;
		r.corners = {ice, grass, ice, grass};
		const auto render = [&](const TerrainVisual::Catalog &c)
		{
			TerrainVisual::Compositor compositor(c);
			compositor.prepare(false, 0);
			GAGCore::DrawableSurface result(32, 32);
			compositor.compose(r, result.getSDLSurface(), 0, 0, 1);
			std::array<unsigned, 32> brightness{};
			for (int y = 0; y < 32; ++y)
			{
				const auto *row = reinterpret_cast<const Uint32 *>(
					static_cast<const unsigned char *>(result.getSDLSurface()->pixels) +
					y * result.getSDLSurface()->pitch);
				for (int x = 0; x < 32; ++x)
					brightness[x] += (row[x] >> 16 & 255) + (row[x] >> 8 & 255) + (row[x] & 255);
			}
			return brightness;
		};
		const auto plain = render(definitions);
		auto shaded = definitions;
		shaded.materials[ice].seam = {4, 128, 768, 0, 0};
		shaded.materials[grass].seam = {2, 128, 768, 0, 0};
		const auto dark = render(shaded);
		auto frosted = definitions;
		frosted.materials[ice].seam = {4, 0, 0, 256, 768, {255, 255, 255}};
		const auto light = render(frosted);
		for (int x = 0; x < 32; ++x)
		{
			INFO(x);
			if (x < 16 || x >= 20)
			{
				CHECK(dark[x] == plain[x]); // Ice casts only downhill, grass not at all.
				CHECK(light[x] == plain[x]);
			}
			else if (x < 18)
			{
				CHECK(dark[x] < plain[x] * 9 / 10);
				CHECK(light[x] > plain[x] * 11 / 10);
			}
		}
	}
	TEST_CASE("surf washes a beach within eight pixels and repeats each period [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto definitions = catalog();
		definitions.compiledPack.clear();
		definitions.boundaryWarp = {}; // A straight water/sand edge at x = 16.
		for (auto &profile : definitions.profiles)
			profile.roughness = profile.speckle = 0;
		const auto water = definitions.find("water"), sand = definitions.find("sand");
		const auto surf = definitions.materials[water].surf;
		REQUIRE(surf.period > 0);
		REQUIRE(definitions.materials[sand].surf.period == 0);
		TerrainVisual::Recipe r;
		r.width = r.height = 16;
		r.x = 3;
		r.y = 5;
		r.corners = {water, sand, water, sand};
		using Columns = std::array<unsigned, 32>;
		const auto render = [&](TerrainVisual::Compositor &compositor, int time)
		{
			compositor.prepare(false, time);
			GAGCore::DrawableSurface result(32, 32);
			compositor.compose(r, result.getSDLSurface(), 0, 0, 1);
			Columns brightness{};
			for (int y = 0; y < 32; ++y)
			{
				const auto *row = reinterpret_cast<const Uint32 *>(
					static_cast<const unsigned char *>(result.getSDLSurface()->pixels) +
					y * result.getSDLSurface()->pitch);
				for (int x = 0; x < 32; ++x)
					brightness[x] += (row[x] >> 16 & 255) + (row[x] >> 8 & 255) + (row[x] & 255);
			}
			return brightness;
		};
		auto still = definitions;
		for (auto &material : still.materials)
			material.surf = {};
		TerrainVisual::Compositor surfCompositor(definitions), stillCompositor(still);
		std::vector<Columns> frames;
		int darker = 0, lighter = 0;
		for (int step = 0; step < 8; ++step)
		{
			const int time = step * surf.period / 8;
			// Water animates its texture as well, so compare at the same time.
			const auto plain = render(stillCompositor, time);
			const auto washed = render(surfCompositor, time);
			for (int x = 0; x < 32; ++x)
			{
				INFO("time " << time << " x " << x);
				if (x < 8 || x >= 24)
					CHECK(washed[x] == plain[x]); // Bands end before the cell's edges.
			}
			CHECK(washed[12] > plain[12]); // Shallows lighten the water near the beach.
			darker += washed[19] < plain[19];
			lighter += washed[19] > plain[19];
			frames.push_back(washed);
		}
		// Wet sand when the wash has drained; foam and a water sheet as it arrives.
		CHECK(darker > 0);
		CHECK(lighter > 0);
		CHECK(std::count(frames.begin(), frames.end(), frames[0]) == 1); // It moves.
		// After a whole surf period and a whole texture cycle the frame repeats.
		const auto &textured = definitions.materials[water];
		const int cycle = std::lcm(surf.period, textured.animationFrames * textured.animationTicks);
		CHECK(render(surfCompositor, cycle + surf.period / 8) == frames[1]);
		// Surf time advances independently of static source artwork.
		const auto dark = definitions.find("dark_water");
		REQUIRE(definitions.materials[dark].animationFrames == 1);
		REQUIRE(definitions.materials[dark].surf.period > 0);
		CHECK(surfCompositor.animated(dark));
		// Dark water keeps its artwork revision even when a surf step advances.
		const auto &darkSurf = definitions.materials[dark].surf;
		REQUIRE(darkSurf.ticks > 1);
		const int step = 7 * darkSurf.ticks;
		surfCompositor.prepare(false, step);
		const auto revision = surfCompositor.textureRevision(dark);
		surfCompositor.prepare(false, step);
		CHECK(surfCompositor.textureRevision(dark) == revision);
		surfCompositor.prepare(false, step + darkSurf.ticks - 1);
		CHECK(surfCompositor.textureRevision(dark) == revision);
		surfCompositor.prepare(false, step + darkSurf.ticks);
		CHECK(surfCompositor.textureRevision(dark) == revision);
		CHECK(surfCompositor.surfTime() == unsigned(step + darkSurf.ticks));
		// Within one step the composed wash is identical.
		const auto washed = [&](int time)
		{
			std::vector<unsigned> result;
			for (int margin = 0; margin < 2048; margin += 64)
				for (bool surfSide : {false, true})
				{
					unsigned value[3] = {180, 160, 120};
					TerrainVisual::applySurf(darkSurf, surfSide, margin, 5000, 9000, unsigned(time), value);
					result.insert(result.end(), value, value + 3);
				}
			return result;
		};
		// Land with a calmer shore wash shortens the wash's reach and swing but
		// keeps every band at full strength; the default full wash is unscaled.
		const auto wash = [&](int land)
		{
			// Peak colour change, and the swing: the farthest land pixel whose
			// colour changes over the period as the wash runs in and out.
			unsigned peak = 0, reach = 0;
			for (int margin = 0; margin < 2048; margin += 16)
				for (bool surfSide : {false, true})
				{
					std::array<unsigned, 3> first{};
					bool moves = false;
					for (int time = 0; time < darkSurf.period; time += darkSurf.ticks)
					{
						unsigned value[3] = {180, 160, 120};
						TerrainVisual::applySurf(darkSurf, surfSide, margin, 5000, 9000, unsigned(time), value, land);
						const unsigned delta = unsigned(std::abs(int(value[0]) - 180) + std::abs(int(value[1]) - 160) +
													   std::abs(int(value[2]) - 120));
						peak = std::max(peak, delta);
						if (!time)
							first = {value[0], value[1], value[2]};
						else
							moves |= first != std::array<unsigned, 3>{value[0], value[1], value[2]};
					}
					if (moves && !surfSide) reach = std::max(reach, unsigned(margin));
				}
			return std::pair{peak, reach};
		};
		const auto full = wash(256), calm = wash(77), none = wash(0);
		CHECK(calm.second < full.second);
		CHECK(none.second <= calm.second);
		// The foam and bands stay about as visible as on a beach; only a long run
		// stacks the sheet over the wet band. Damping the bands too would leave
		// the calm peak near 30% of the full one.
		CHECK(calm.first * 4 >= full.first * 3);
		CHECK(none.first * 4 >= full.first * 3);
		for (int margin = 0; margin < 2048; margin += 64)
		{
			unsigned a[3] = {180, 160, 120}, b[3] = {180, 160, 120};
			TerrainVisual::applySurf(darkSurf, false, margin, 5000, 9000, 40, a);
			TerrainVisual::applySurf(darkSurf, false, margin, 5000, 9000, 40, b, 256);
			CHECK(std::equal(a, a + 3, b));
		}
		// Laid paths take the calmer wash.
		CHECK(definitions.materials[definitions.find("road")].shoreWash == 77);
		CHECK(definitions.materials[definitions.find("grass")].shoreWash == 256);
		bool moved = false;
		for (int k = 0; k < darkSurf.period; k += darkSurf.ticks)
		{
			for (int t = 1; t < darkSurf.ticks; ++t)
				CHECK(washed(k + t) == washed(k));
			moved = moved || washed(k) != washed(k + darkSurf.ticks);
		}
		CHECK(moved);
	}
	TEST_CASE("shore calm quiets waves toward land without shifting the water's colour [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		using TerrainVisual::shoreCalmAt;
		// Full calm at the shoreline, fading out within the reach and smoothly.
		CHECK(shoreCalmAt(0) == 0);
		CHECK(shoreCalmAt(1) == 215); // The adjacent water vertex is half a tile from shore.
		CHECK(shoreCalmAt(1) > shoreCalmAt(2));
		CHECK(shoreCalmAt(2) > 0);
		CHECK(shoreCalmAt(4) == 40); // Waves still calm at one and a half tiles.
		CHECK(shoreCalmAt(9) == 0); // Beyond the two-tile reach.
		CHECK(TerrainVisual::shoreCalm({255, 255, 255, 255}, 37, 200) == 255);
		CHECK(TerrainVisual::shoreCalm({}, 37, 200) == 0);
		CHECK(TerrainVisual::shoreCalm({200, 0, 200, 0}, 0, 128) == 200);
		CHECK(TerrainVisual::shoreCalm({200, 0, 200, 0}, 256, 128) == 0);

		auto definitions = catalog();
		definitions.compiledPack.clear();
		TerrainVisual::Compositor compositor(definitions);
		const auto &materials = compositor.catalog().materials;
		const auto water = definitions.find("water"), deep = definitions.find("deep_water");
		CHECK(materials[water].shoreCalm);
		CHECK(materials[deep].shoreCalm);
		CHECK_FALSE(materials[definitions.find("dark_water")].shoreCalm); // Its surface is still.
		CHECK_FALSE(materials[definitions.find("sand")].shoreCalm);
		// Calm keeps the colour and cuts the wave contrast, at every phase.
		for (const auto id : {water, deep})
			for (int time : {0, 37, 211})
			{
				CAPTURE(time);
				compositor.prepare(false, time);
				TerrainVisual::Recipe r;
				r.width = r.height = 64;
				r.x = 9;
				r.y = 13;
				r.corners = {id, id, id, id};
				const auto statistics = [&](const TerrainVisual::Recipe &recipe)
				{
					GAGCore::DrawableSurface result(32, 32);
					compositor.compose(recipe, result.getSDLSurface(), 0, 0, 1);
					double sum[3] = {}, luma = 0, luma2 = 0;
					for (int y = 0; y < 32; ++y)
						for (int x = 0; x < 32; ++x)
						{
							const auto pixel = reinterpret_cast<const Uint32 *>(
								static_cast<const unsigned char *>(result.getSDLSurface()->pixels) +
								y * result.getSDLSurface()->pitch)[x];
							const double rgb[3] = {double(pixel >> 16 & 255), double(pixel >> 8 & 255),
												   double(pixel & 255)};
							for (int k = 0; k < 3; ++k)
								sum[k] += rgb[k] / 1024;
							const double l = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2];
							luma += l / 1024;
							luma2 += l * l / 1024;
						}
					return std::array<double, 4>{sum[0], sum[1], sum[2], std::sqrt(luma2 - luma * luma)};
				};
				const auto open = statistics(r);
				r.calm = {255, 255, 255, 255};
				const auto shore = statistics(r);
				for (int k = 0; k < 3; ++k)
					CHECK(std::abs(shore[k] - (open[k] + (materials[id].calmMean[k] - open[k]) * 0.75)) < 1.5);
				CHECK(shore[3] < open[3] * 0.35);
			}

		// describe() derives the calm from the distance to land.
		glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .terrain = WATER, .discovered = true});
		for (int y = 0; y < 32; ++y)
			fixture.game.map.setVertexTerrain(16, y, SAND);
		SceneMap scene;
		glob2test::observeMap(fixture.game.map, scene);
		auto &game = globals->terrainCompositor();
		const auto at = [&](int x) { return game.describe(scene, x, 7); };
		CHECK_FALSE(at(4).calmed()); // Open water keeps its waves.
		CHECK(at(15).calm == std::array<std::uint8_t, 4>{at(15).calm[0], 255, at(15).calm[0], 255});
		CHECK(at(15).calm[0] == shoreCalmAt(1));
		CHECK(at(14).calm[1] == at(15).calm[0]); // Corners are shared, so the fade is continuous.
		CHECK(at(14).calm[0] == shoreCalmAt(4));
		CHECK(at(16).calm == std::array<std::uint8_t, 4>{255, at(15).calm[0], 255, at(15).calm[0]});
	}
	TEST_CASE("mixed cells compose exactly as a direct per-pixel coverage blend [display]")
	{
		// The reference is the blend before cell masks: sample coverage per pixel
		// and blend the four entries in place, with the same seam toning.
		glob2test::HeadlessGlobals globals({.display = true});
		auto definitions = catalog();
		definitions.compiledPack.clear();
		TerrainVisual::Compositor compositor(definitions);
		compositor.prepare(false, 0);
		const auto &c = compositor.catalog();
		const auto water = c.find("water"), sand = c.find("sand"), grass = c.find("grass"),
				   deep = c.find("deep_water"), ice = c.find("ice");
		const auto pixels = [](GAGCore::DrawableSurface &s, int x, int y)
		{
			return reinterpret_cast<const Uint32 *>(
				static_cast<const unsigned char *>(s.getSDLSurface()->pixels) +
				y * s.getSDLSurface()->pitch)[x];
		};
		const std::vector<std::array<TerrainVisual::MaterialId, 4>> mixes = {
			{water, sand, water, sand},	  {water, water, water, grass}, {grass, water, water, grass},
			{water, sand, grass, deep},	  {deep, water, sand, sand},	{ice, water, grass, ice},
			{sand, grass, water, water}};
		for (std::uint32_t seed : {0u, 7u})
			for (int scale : {1, 2})
				for (const auto &corners : mixes)
				{
					TerrainVisual::Recipe r;
					r.width = r.height = 32;
					r.x = 5;
					r.y = 9;
					r.seed = seed;
					r.corners = corners;
					// Each material's texture as this cell selects it: a uniform cell copies it.
					std::map<TerrainVisual::MaterialId, std::vector<Uint32>> textures;
					for (auto id : corners)
					{
						auto uniform = r;
						uniform.corners = {id, id, id, id};
						GAGCore::DrawableSurface tile(32, 32);
						compositor.compose(uniform, tile.getSDLSurface(), 0, 0, 1);
						auto &t = textures[id];
						for (int y = 0; y < 32; ++y)
							for (int x = 0; x < 32; ++x)
								t.push_back(pixels(tile, x, y));
					}
					const int size = 32 * scale;
					GAGCore::DrawableSurface actual(size, size);
					compositor.compose(r, actual.getSDLSurface(), 0, 0, scale);
					const TerrainVisual::PreparedCoverage prepared(c, r);
					unsigned mismatches = 0;
					for (int y = 0; y < size; ++y)
						for (int x = 0; x < size; ++x)
						{
							const auto mask = prepared.at((x * 256 + 128) / scale, (y * 256 + 128) / scale);
							std::uint64_t rgb[3] = {};
							unsigned alpha = 0;
							for (int i = 0; i < 4; ++i)
								if (mask.weight[i])
								{
									const Uint32 p = textures.at(mask.material[i])[(y * 32 / size) * 32 + x * 32 / size];
									const unsigned channels[] = {p >> 16 & 255, p >> 8 & 255, p & 255};
									const unsigned a = mask.weight[i] * (p >> 24);
									alpha += a;
									for (int k = 0; k < 3; ++k)
										rgb[k] += std::uint64_t(channels[k]) * a;
								}
							unsigned dominant = 0;
							for (unsigned i = 1; i < 4; ++i)
								if (mask.weight[i] > mask.weight[dominant])
									dominant = i;
							const auto &self = c.materials[mask.material[dominant]].seam;
							const auto &other = c.materials[mask.neighbor].seam;
							int shade = 256, tint = 0;
							if (mask.neighbor != mask.material[dominant])
							{
								if (c.materials[mask.neighbor].decor.connected.empty() && other.cast &&
									other.height > self.height && int(mask.margin) < other.castWidth)
									shade = 256 - other.cast * (other.castWidth - int(mask.margin)) / other.castWidth;
								if (other.fringe && int(mask.margin) < other.fringeWidth)
									tint = other.fringe * (other.fringeWidth - int(mask.margin)) / other.fringeWidth;
							}
							const auto channel = [&](int k)
							{
								unsigned value = unsigned(alpha ? rgb[k] / alpha : 0) * shade >> 8;
								return value + (unsigned(other.fringeColor[k]) - value) * tint / 256;
							};
							unsigned value[3] = {channel(0), channel(1), channel(2)};
							const auto &selfSurf = c.materials[mask.material[dominant]].surf;
							const auto &otherSurf = c.materials[mask.neighbor].surf;
							if ((selfSurf.period > 0) != (otherSurf.period > 0))
								TerrainVisual::applySurf(selfSurf.period ? selfSurf : otherSurf,
														 selfSurf.period > 0, int(mask.margin),
														 r.x * 8192 + (2 * x + 1) * 4096 / size,
														 r.y * 8192 + (2 * y + 1) * 4096 / size, 0, value,
														 c.materials[selfSurf.period ? mask.neighbor
																					 : mask.material[dominant]].shoreWash);
							const Uint32 expected = ((alpha + 32768) / 65536 << 24) | (value[0] << 16) |
													(value[1] << 8) | value[2];
							mismatches += pixels(actual, x, y) != expected;
						}
					INFO("seed " << seed << " scale " << scale << " corners " << corners[0] << ","
								 << corners[1] << "," << corners[2] << "," << corners[3]);
					CHECK(mismatches == 0);
				}
	}
	TEST_CASE("map seed reseeds variants and boundaries without breaking partitions or edges")
	{
		glob2test::HeadlessGlobals globals;
		const auto c = catalog();
		const auto grass = c.find("grass"), sand = c.find("sand");
		unsigned differentVariants = 0;
		const int grid = c.materials[grass].variantGrid;
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				differentVariants += c.variantIndex(grass, x * grid, y * grid, 1) !=
									c.variantIndex(grass, x * grid, y * grid, 2);
		CHECK(differentVariants > 600); // Four independently seeded layouts.
		CHECK(c.variantIndex(grass, 5, 6) == c.variantIndex(grass, 5, 6, 0));
		TerrainVisual::Recipe r;
		r.width = r.height = 16;
		r.x = 4;
		r.y = 9;
		r.corners = {grass, sand, grass, sand};
		unsigned differentPixels = 0;
		for (std::uint32_t seed : {0u, 1u, 0xdeadbeefu})
		{
			TerrainVisual::Recipe seeded = r;
			seeded.seed = seed;
			TerrainVisual::Recipe wrapped = seeded;
			wrapped.x = r.width + r.x; // Same canonical cell reached through wrapping.
			wrapped.x %= r.width;
			const TerrainVisual::PreparedCoverage a(c, seeded), b(c, r), w(c, wrapped);
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
				{
					const auto p = a.at(x * 256 + 128, y * 256 + 128);
					unsigned total = 0;
					for (int k = 0; k < 4; ++k)
						total += p.weight[k];
					CHECK(total == 65536);
					CHECK(p.weight == w.at(x * 256 + 128, y * 256 + 128).weight);
					differentPixels += p.weight != b.at(x * 256 + 128, y * 256 + 128).weight;
				}
		}
		CHECK(differentPixels > 100); // Seeds 1 and 0xdeadbeef move the edge; seed 0 is the base.
		// Both tiles along a shared edge agree for any seed, as the seedless test checks.
		TerrainVisual::Recipe left = r, right = r;
		left.seed = right.seed = 77;
		right.x = r.x + 1; // The right tile's left corners are the left tile's right corners.
		left.corners = {0, 1, 2, 3};
		right.corners = {1, 4, 3, 0};
		for (int y = 0; y < 32; ++y)
		{
			const auto p = TerrainVisual::coverage(c, left, 8192, y * 256),
					   q = TerrainVisual::coverage(c, right, 0, y * 256);
			std::array<unsigned, 5> pp{}, qq{};
			for (int k = 0; k < 4; ++k)
			{
				pp[p.material[k]] += p.weight[k];
				qq[q.material[k]] += q.weight[k];
			}
			CHECK(pp == qq);
		}
	}
	TEST_CASE("prepared coverage preserves native and HD contour geometry")
	{
		glob2test::HeadlessGlobals globals;
		for (bool legacy : {false, true})
		{
			auto definitions = catalog();
			if (legacy)
			{
				auto j = shippedCatalog();
				j["version"] = 1;
				j.erase("boundary_warp_q8");
				j.erase("border_classes");
				for (auto &material : j["materials"])
				{
					material.erase("seam");
					material.erase("surf");
					material.erase("class");
					material.erase("stones");
					material.erase("shore_wash_q8");
				}
				const int roughness[] = {256, 320, 192};
				for (unsigned i = 0; i < j["profiles"].size(); ++i)
				{
					auto &p = j["profiles"][i];
					for (const char *field : {"feather_q8", "amplitude_q8", "speckle_q8", "bridge_q8", "shape",
							 "precedence", "coast", "wiggle_q8", "wiggle_period", "squareness_q8", "teeth_q8",
							 "teeth_form", "facets", "clasts", "resistance_q8", "cohesion_q8", "grain_q8",
							 "spread_q8"})
						p.erase(field);
					p["roughness_q8"] = roughness[i % std::size(roughness)];
					p["contours_q12"] = {{0, 180, -120, 100, 0},
										 {0, -130, 200, -80, 0},
										 {0, 90, 160, -170, 0},
										 {0, -180, -60, 140, 0}};
				}
				definitions = TerrainVisual::Catalog::parse(j);
			}
			std::uint64_t digest = 14695981039346656037ull;
			for (unsigned configuration = 0; configuration < 256; ++configuration)
			{
				TerrainVisual::Recipe recipe;
				recipe.width = recipe.height = 16;
				recipe.x = 15;
				recipe.y = 9;
				for (int k = 0; k < 4; ++k)
					recipe.corners[k] = (configuration >> (2 * k)) & 3;
				const TerrainVisual::PreparedCoverage prepared(definitions, recipe);
				for (int scale : {1, 4})
					for (int y = 0; y < 32 * scale; ++y)
						for (int x = 0; x < 32 * scale; ++x)
						{
							const auto pixel =
								prepared.at((x * 256 + 128) / scale, (y * 256 + 128) / scale);
							for (int k = 0; k < 4; ++k)
							{
								digest = (digest ^ pixel.material[k]) * 1099511628211ull;
								digest = (digest ^ pixel.weight[k]) * 1099511628211ull;
							}
						}
			}
			// Fingerprint the reviewed native/HD geometry. Intentional contour changes
			// require a rendered comparison and an updated digest, not just a
			// matching partition sum.
			CHECK(digest == (legacy ? 4747172408471892537ull : 8938740251159767833ull));
		}
	}
	TEST_CASE("the Python validator's built-in name list mirrors the terrain table")
	{
		std::ifstream input(glob2test::sourceRoot() / "tools/terrain_builtin_names.json");
		REQUIRE(input);
		const auto listed = nlohmann::json::parse(input);
		REQUIRE(listed.is_array());
		std::vector<std::string> expected;
		for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
			if (terrainPaintable(TerrainType(i)))
				expected.push_back(terrainPresentation(TerrainType(i)).name);
		std::vector<std::string> actual;
		for (const auto &name : listed)
			actual.push_back(name.get<std::string>());
		CHECK(actual == expected);
	}
	TEST_CASE("catalogue boundary profiles keep their reviewed geometry")
	{
		glob2test::HeadlessGlobals globals;
		const auto definitions = catalog();
		// Reviewed representatives for rock, soil, crisp and the quiet canopy underlay.
		const std::array<TerrainVisual::MaterialId, 4> samples{
			definitions.find("boulders"), definitions.find("mud"), definitions.find("void_hole"),
			definitions.find("hedge")};
		for (auto id : samples)
			CHECK(definitions.materials[id].profile != definitions.materials[definitions.find("grass")].profile);
		std::uint64_t digest = 14695981039346656037ull;
		for (unsigned configuration = 0; configuration < 256; ++configuration)
		{
			TerrainVisual::Recipe recipe;
			recipe.width = recipe.height = 16;
			recipe.x = 15;
			recipe.y = 9;
			for (int k = 0; k < 4; ++k)
				recipe.corners[k] = samples[(configuration >> (2 * k)) & 3];
			const TerrainVisual::PreparedCoverage prepared(definitions, recipe);
			for (int scale : {1, 4})
				for (int y = 0; y < 32 * scale; ++y)
					for (int x = 0; x < 32 * scale; ++x)
					{
						const auto pixel = prepared.at((x * 256 + 128) / scale, (y * 256 + 128) / scale);
						for (int k = 0; k < 4; ++k)
						{
							digest = (digest ^ pixel.material[k]) * 1099511628211ull;
							digest = (digest ^ pixel.weight[k]) * 1099511628211ull;
						}
					}
		}
		// This hashes catalog IDs as well as coverage. The group-look catalog
		// reordered mud (14 -> 12) and void_hole (27 -> 23) after the reviewed
		// semantic-border update, preserving its profiles. Geometry changes still
		// need a rendered comparison.
		CHECK(digest == 12872139009941546255ull);
	}
	TEST_CASE("coverage partitions every binary shape and multi-material junction")
	{
		glob2test::HeadlessGlobals globals;
		const auto c = catalog();
		for (unsigned configuration = 0; configuration < 256; ++configuration)
		{
			TerrainVisual::Recipe r;
			r.width = r.height = 16;
			r.x = 7;
			r.y = 9;
			for (int k = 0; k < 4; ++k)
				r.corners[k] = (configuration >> (2 * k)) & 3;
			const TerrainVisual::PreparedCoverage prepared(c, r);
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
				{
					const auto a = prepared.at(x * 256 + 128, y * 256 + 128);
					unsigned total = 0;
					for (int i = 0; i < 4; ++i)
					{
						CHECK(a.material[i] < c.materials.size());
						total += a.weight[i];
					}
					CHECK(total == 65536);
				}
		}
	}
	TEST_CASE("shared edges and wrapped samples have identical coverage")
	{
		glob2test::HeadlessGlobals globals;
		const auto c = catalog();
		TerrainVisual::Recipe a;
		a.width = a.height = 16;
		a.x = 15;
		a.y = 15;
		TerrainVisual::Recipe b = a;
		b.x = 0;
		a.corners = {0, 1, 2, 3};
		b.corners = {1, 4, 3, 0}; // Left corners equal a's right corners.
		for (int y = 0; y < 32; ++y)
		{
			const auto p = TerrainVisual::coverage(c, a, 8192, y * 256),
					   q = TerrainVisual::coverage(c, b, 0, y * 256);
			std::array<unsigned, 5> pp{}, qq{};
			for (int k = 0; k < 4; ++k)
			{
				pp[p.material[k]] += p.weight[k];
				qq[q.material[k]] += q.weight[k];
			}
			CHECK(pp == qq);
		}
	}
	TEST_CASE("diagonal contacts stay distinct and both torus axes share coverage")
	{
		glob2test::HeadlessGlobals globals;
		auto c = catalog();
		c.boundaryWarp = {}; // Isolate the unperturbed diagonal connectivity rule.
		for (auto &profile : c.profiles)
			profile.roughness = profile.speckle = profile.bridge = 0;
		TerrainVisual::Recipe r;
		r.width = r.height = 16;
		r.corners = {c.find("road"), c.find("grass"), c.find("grass"), c.find("road")};
		// Along a diagonal each region is solid up to the narrow junction;
		// merging their bilinear weights used to create a broad saddle fade.
		const auto interior = coverage(c, r, 14 * 256, 14 * 256);
		unsigned road = 0;
		for (int k = 0; k < 4; ++k)
			if (interior.material[k] == c.find("road"))
				road += interior.weight[k];
		CHECK(road == 65536);
		TerrainVisual::Recipe a = r, b = r;
		a.y = 15;
		b.y = 0;
		a.corners = {0, 1, 2, 3};
		b.corners = {2, 3, 4, 0}; // Top corners equal a's bottom corners.
		for (int x = 0; x < 32; ++x)
		{
			const auto p = coverage(c, a, x * 256, 8192), q = coverage(c, b, x * 256, 0);
			std::array<unsigned, 5> pp{}, qq{};
			for (int k = 0; k < 4; ++k)
			{
				pp[p.material[k]] += p.weight[k];
				qq[q.material[k]] += q.weight[k];
			}
			CHECK(pp == qq);
		}
	}
	TEST_CASE("organic contours agree across ordinary tile edges and both torus axes")
	{
		glob2test::HeadlessGlobals globals;
		auto c = catalog();
		c.boundaryWarp = {1024, 384, 128}; // Exercise every optional scale at its maximum.
		for (int size : {1, 2, 16})
			for (int axis = 0; axis < 2; ++axis)
				for (int position = 0; position < size; ++position)
				{
					const auto recipe = [&](int x, int y)
					{
						TerrainVisual::Recipe r;
						r.width = r.height = size;
						r.x = x % size;
						r.y = y % size;
						// Corner materials hashed from wrapped vertex coordinates.
						for (int k = 0; k < 4; ++k)
							r.corners[k] = TerrainVisual::hash((r.x + (k & 1)) % size,
															   (r.y + (k >> 1)) % size) %
										   5;
						return r;
					};
					const TerrainVisual::PreparedCoverage a(c, recipe(position, position));
					const TerrainVisual::PreparedCoverage b(
						c, recipe(position + (axis == 0), position + (axis == 1)));
					// Quarter-pixel sampling covers HD and native positions, including
					// noise-grid endpoints and the shared corner of four render tiles.
					for (int t = 0; t <= 8192; t += 64)
					{
						const auto p = axis ? a.at(t, 8192) : a.at(8192, t);
						const auto q = axis ? b.at(t, 0) : b.at(0, t);
						std::array<unsigned, 5> pp{}, qq{};
						for (int k = 0; k < 4; ++k)
						{
							pp[p.material[k]] += p.weight[k];
							qq[q.material[k]] += q.weight[k];
						}
						CHECK(pp == qq);
					}
				}
	}
	TEST_CASE("straight boundaries bend over multiple cells without moving their solid cores")
	{
		glob2test::HeadlessGlobals globals;
		auto c = catalog();
		for (auto &profile : c.profiles)
			profile.roughness = 0; // Measure the world field independently of local motifs.
		const auto grass = c.find("grass"), ice = c.find("ice");
		int minimum = 8192, maximum = 0;
		std::set<std::array<int, 32>> segments;
		for (int cy = 0; cy < 16; ++cy)
		{
			TerrainVisual::Recipe r;
			r.width = r.height = 16;
			r.x = 4;
			r.y = cy;
			r.corners = {grass, ice, grass, ice};
			const TerrainVisual::PreparedCoverage prepared(c, r);
			std::array<int, 32> segment{};
			for (int y = 0; y < 32; ++y)
			{
				for (int x = 8 * 256; x <= 24 * 256; x += 64)
				{
					const auto sample = prepared.at(x, y * 256);
					unsigned iceWeight = 0;
					for (int k = 0; k < 4; ++k)
						if (sample.material[k] == ice)
							iceWeight += sample.weight[k];
					if (x == 8 * 256)
						CHECK(iceWeight == 0);
					if (x == 24 * 256)
						CHECK(iceWeight == 65536);
					if (iceWeight >= 32768 && !segment[y])
						segment[y] = x;
				}
				minimum = std::min(minimum, segment[y]);
				maximum = std::max(maximum, segment[y]);
			}
			segments.insert(segment);
		}
		CHECK(maximum - minimum >= 3 * 256);
		CHECK(segments.size() == 16);
	}
	TEST_CASE("steep detailed contours retain one crossing on shared patch edges")
	{
		glob2test::HeadlessGlobals globals;
		auto c = catalog();
		c.boundaryWarp = {};
		for (auto &profile : c.profiles)
		{
			profile.roughness = 512;
			profile.speckle = 0; // Pebbles may straddle an edge; contours may not fold it.
			for (auto &curve : profile.contours)
			{
				curve.assign(33, 0);
				for (int i = 1; i < 32; ++i)
					curve[i] = i % 2 ? 512 : -512;
			}
		}
		for (int position = 0; position < 16; ++position)
		{
			TerrainVisual::Recipe r;
			r.width = r.height = 16;
			r.x = r.y = position;
			r.corners = {c.find("grass"), c.find("ice"), c.find("grass"), c.find("ice")};
			const TerrainVisual::PreparedCoverage prepared(c, r);
			for (int y : {8, 24})
			{
				unsigned previous = 0;
				for (int x = 0; x <= 8192; x += 32)
				{
					const auto sample = prepared.at(x, y * 256);
					unsigned weight = 0;
					for (int k = 0; k < 4; ++k)
						if (sample.material[k] == c.find("ice"))
							weight += sample.weight[k];
					CHECK(weight >= previous);
					previous = weight;
				}
				CHECK(previous == 65536);
			}
		}
	}

	TEST_CASE("weighted choices are stable and all existing variants are reachable")
	{
		glob2test::HeadlessGlobals globals;
		const auto c = catalog();
		for (unsigned id = 0; id < c.materials.size(); ++id)
		{
			const auto &material = c.materials[id];
			INFO(material.key);
			const int cycle = material.animationFrames * material.animationTicks;
			std::set<int> seen;
			for (int y = 0; y < 64; ++y)
				for (int x = 0; x < 64; ++x)
				{
					auto frame = c.frame(id, x, y, 0);
					seen.insert(frame);
					// The artwork may animate; its spatial variant stays the same
					// at every matching phase, even a hundred cycles later.
					for (int phase = 0; phase < material.animationFrames; ++phase)
					{
						const int time = phase * material.animationTicks;
						const auto atPhase = c.frame(id, x, y, time);
						if (phase > 0)
							CHECK(atPhase == frame + phase * material.animationStride);
						CHECK(atPhase == c.frame(id, x, y, time + 100 * cycle));
					}
				}
			CHECK(seen.size() == c.materials[id].variants.size());
		}
	}

	TEST_CASE("grid variants repeat one positional block independent of the map seed")
	{
		glob2test::HeadlessGlobals globals;
		auto c = catalog();
		const auto water = c.find("water");
		const unsigned grid = unsigned(c.materials[water].variantGrid);
		c.materials[water].variantBlocks = 1;
		c.materials[water].variants.resize(grid * grid);
		for (std::uint32_t seed : {0u, 1u, 0xdeadbeefu})
			for (int y = -4; y < 12; ++y)
				for (int x = -4; x < 12; ++x)
				{
					INFO(seed << " " << x << " " << y);
					CHECK(c.variantIndex(water, x, y, seed) == (unsigned(y) & (grid - 1)) * grid + (unsigned(x) & (grid - 1)));
				}

		const auto j = shippedCatalog();
		std::size_t index = 0;
		while (j["materials"][index]["key"] != "water")
			++index;
		auto invalid = j;
		invalid["materials"][index]["variant_grid"] = 3;
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		invalid = j;
		invalid["materials"][index]["variant_grid"] = 16; // Four complete8x8 layouts, not16x16.
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		invalid = j;
		invalid["materials"][index].erase("edges");
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
	}

	TEST_CASE("interchangeable grid blocks select one seeded layout for every cell in a block")
	{
		glob2test::HeadlessGlobals globals;
		const auto c = catalog();
		// The waters stay on one layout: every layout multiplies their sixteen phases.
		// Lava and ember field have four layouts like static ground.
		std::set<std::string> single;
		for (const auto &material : c.materials)
		{
			const auto id = c.find(material.key);
			if (c.materials[id].variantBlocks == 1)
			{
				CHECK(c.materials[id].animationFrames > 1);
				CHECK(c.materials[id].variantGrid > 0);
				single.insert(material.key);
				continue;
			}
			REQUIRE(c.materials[id].variantBlocks == 4);
			const int grid = c.materials[id].variantGrid;
			const unsigned cells = unsigned(grid * grid);
			REQUIRE(grid == 8);
			std::set<unsigned> seen;
			unsigned changed = 0;
			for (int by = -4; by < 28; ++by)
				for (int bx = -4; bx < 28; ++bx)
				{
					const unsigned layout = c.variantIndex(id, bx * grid, by * grid, 42) / cells;
					seen.insert(layout);
					changed += layout != c.variantIndex(id, bx * grid, by * grid, 43) / cells;
					for (int y = 0; y < grid; ++y)
						for (int x = 0; x < grid; ++x)
						{
							const unsigned index = c.variantIndex(id, bx * grid + x, by * grid + y, 42);
							CHECK(index == layout * cells + unsigned(y * grid + x));
							CHECK(index == c.variantIndex(id, bx * grid + x, by * grid + y, 42));
						}
				}
			CHECK(seen.size() == 4);
			CHECK(changed > 600);
		}
		CHECK(single == std::set<std::string>{"water", "deep_water"});
		for (unsigned seed : {0u, 1u, 42u})
			for (int y = -8; y < 64; ++y)
				for (int x = -8; x < 64; ++x)
					CHECK(c.variantIndex(c.find("water"), x, y, seed) == c.variantIndex(c.find("deep_water"), x, y, seed));
		const auto document = shippedCatalog();
		std::size_t index = 0;
		while (document["materials"][index]["key"] != "scree") ++index;
		for (const auto &value : {nlohmann::json(0), nlohmann::json(2), nlohmann::json(17), nlohmann::json("4")})
		{
			auto invalid = document;
			invalid["materials"][index]["variant_blocks"] = value;
			CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		}
		for (const auto &value : {nlohmann::json(""), nlohmann::json(7), nlohmann::json(std::string(65, 'x'))})
		{
			auto invalid = document;
			invalid["materials"][index]["variant_block_group"] = value;
			CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		}
		auto invalid = document;
		invalid["materials"][index].erase("variant_grid");
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
	}

	TEST_CASE("catalog rejects corrupt references and grows independently of gameplay IDs")
	{
		glob2test::HeadlessGlobals globals;
		auto j = shippedCatalog();
		for (auto i = j["materials"].size(); i < 64; ++i)
		{
			auto m = j["materials"][2];
			m["key"] = "fixture-" + std::to_string(i);
			j["materials"].push_back(m);
		}
		CHECK(TerrainVisual::Catalog::parse(j).materials.size() == 64);
		auto patchOnly = j;
		for (auto &profile : patchOnly["profiles"])
			if (profile.value("shape", "") == "contextual") profile.erase("shape");
		const auto withoutShapes = TerrainVisual::Catalog::parse(patchOnly);
		CHECK(std::none_of(withoutShapes.profiles.begin(), withoutShapes.profiles.end(),
			[](const auto &profile) { return profile.shape == TerrainVisual::BoundaryShape::Contextual; }));
		auto legacy = j;
		legacy.erase("boundary_warp_q8");
		CHECK(TerrainVisual::Catalog::parse(legacy).boundaryWarp == std::array<int, 3>{});
		for (int count : {5, 9, 17, 33})
		{
			auto detailed = j;
			auto points = std::vector<int>(count, 512);
			points.front() = points.back() = 0;
			detailed["profiles"][0]["contours_q12"][0] = points;
			CHECK(TerrainVisual::Catalog::parse(detailed).profiles[0].contours[0].size() == count);
		}
		auto invalid = j;
		invalid["materials"][0]["variants"][0]["weight"] = 0;
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		invalid = j;
		invalid["profiles"][0]["contours_q12"][0][0] = 1;
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		invalid = j;
		invalid["bindings"]["ice"] = "absent";
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		invalid = j;
		invalid["bindings"].erase("mud"); // Every paintable built-in needs a material.
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		const std::vector<std::pair<nlohmann::json::json_pointer, nlohmann::json>> malformed = {
			{nlohmann::json::json_pointer("/profiles/0/shape"), "unknown"},
			{nlohmann::json::json_pointer("/profiles/0/shape"), 1},
			{nlohmann::json::json_pointer("/profiles/0/feather_q8"), true},
			{nlohmann::json::json_pointer("/profiles/0/feather_q8"), 127},
			{nlohmann::json::json_pointer("/profiles/0/feather_q8"), 513},
			{nlohmann::json::json_pointer("/profiles/0/contours_q12/0"), {0, 0, 0, 0, 0, 0}},
			{nlohmann::json::json_pointer("/profiles/0/contours_q12/0/1"), 1025},
			{nlohmann::json::json_pointer("/profiles/0/contours_q12"), nlohmann::json::array()},
			{nlohmann::json::json_pointer("/profiles/0/amplitude_q8"), 1025},
			{nlohmann::json::json_pointer("/profiles/0/speckle_q8"), true},
			{nlohmann::json::json_pointer("/profiles/0/bridge_q8"), -1},
			{nlohmann::json::json_pointer("/version"), 2},
			{nlohmann::json::json_pointer("/materials/3/seam"), nlohmann::json::array()},
			{nlohmann::json::json_pointer("/materials/3/seam/cast_q8"), 257},
			{nlohmann::json::json_pointer("/materials/3/seam/height"), -1},
			{nlohmann::json::json_pointer("/materials/3/seam/fringe"), {0, 0}},
			{nlohmann::json::json_pointer("/materials/3/seam/fringe_width_q8"), 2049},
			{nlohmann::json::json_pointer("/materials/3/surf"), nlohmann::json::array()},
			{nlohmann::json::json_pointer("/materials/3/surf"), {{"period", 0}}},
			{nlohmann::json::json_pointer("/materials/3/surf"), {{"period", 60}, {"wet_q8", 257}}},
			{nlohmann::json::json_pointer("/materials/3/surf"), {{"period", 60}, {"ticks", 0}}},
			{nlohmann::json::json_pointer("/materials/3/surf"), {{"period", 60}, {"ticks", 61}}},
			{nlohmann::json::json_pointer("/materials/3/surf"),
			 {{"period", 60}, {"reach_q8", 1800}, {"foam_width_q8", 300}}},
			{nlohmann::json::json_pointer("/materials/3/surf"), {{"period", 60}, {"shallow", {0, 0}}}},
			{nlohmann::json::json_pointer("/boundary_warp_q8"), {0, 0}},
			{nlohmann::json::json_pointer("/boundary_warp_q8"), {true, 0, 0}},
			{nlohmann::json::json_pointer("/boundary_warp_q8"), {0, 1.5, 0}},
			{nlohmann::json::json_pointer("/boundary_warp_q8"), {-1, 0, 0}},
			{nlohmann::json::json_pointer("/boundary_warp_q8"), {1025, 0, 0}},
			{nlohmann::json::json_pointer("/boundary_warp_q8"), {0, 385, 0}},
			{nlohmann::json::json_pointer("/boundary_warp_q8"), {0, 0, 129}},
			{nlohmann::json::json_pointer("/version"), true},
			{nlohmann::json::json_pointer("/version"), 1.0},
			{nlohmann::json::json_pointer("/compiled_pack"), 0},
			{nlohmann::json::json_pointer("/compiled_pack"), "data/terrain/wrong.json"},
			{nlohmann::json::json_pointer("/compiled_pack"), "data/atlas.json"},
			{nlohmann::json::json_pointer("/profiles"), nlohmann::json::object()},
			// Retired: water is an ordinary tile material without a backdrop.
			{nlohmann::json::json_pointer("/materials/0/ocean"), false},
			{nlohmann::json::json_pointer("/materials/0/preview"), {1, 2, true}},
			{nlohmann::json::json_pointer("/materials/0/variants/0/weight"), 1.5},
			{nlohmann::json::json_pointer("/materials/0/animation_frames"), 257},
			{nlohmann::json::json_pointer("/materials/0/edges"), "wrap"},
			{nlohmann::json::json_pointer("/materials/1/backdrop"),
			 {{"sprite", "data/gfx/terrain"}}},
			{nlohmann::json::json_pointer("/bindings"), nlohmann::json::array()},
			{nlohmann::json::json_pointer("/bindings/fixture"), "absent"},
			{nlohmann::json::json_pointer("/pair_treatments"), nlohmann::json::object()},
			{nlohmann::json::json_pointer("/border_classes"), nlohmann::json::array()},
			{nlohmann::json::json_pointer("/border_classes/0/shoreline"), 1},
			{nlohmann::json::json_pointer("/border_classes/1/key"), "cover"},
			{nlohmann::json::json_pointer("/materials/0/class"), "absent"},
			{nlohmann::json::json_pointer("/profiles/0/coast"), "absent"},
			{nlohmann::json::json_pointer("/profiles/0/coast"), "canopy"},
			{nlohmann::json::json_pointer("/profiles/0/precedence"), 256},
			// Version 4 draws every profile smoothly, so a patch profile may meander,
			// but its parameters keep their ranges.
			{nlohmann::json::json_pointer("/profiles/0/wiggle_q8"), 2049},
			{nlohmann::json::json_pointer("/profiles/0/detail_q8"), 1025},
			{nlohmann::json::json_pointer("/profiles/0/detail_q8"), -1},
			{nlohmann::json::json_pointer("/profiles/2/wiggle_period"), 48},
			{nlohmann::json::json_pointer("/profiles/2/wiggle_q8"), 2049},
			{nlohmann::json::json_pointer("/profiles/2/squareness_q8"), 257},
			{nlohmann::json::json_pointer("/profiles/0/squareness_q8"), 257},
		};
		for (const auto &[field, value] : malformed)
		{
			INFO(field.to_string());
			invalid = j;
			invalid[field] = value;
			CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		}
		for (const std::string path :
			 {"data//gfx/ice", "data/gfx/./ice", "data/gfx/../ice", "data/gfx/ice/",
			  "data/gfx/ice\\broken", "data/gfx/ice:broken"})
		{
			INFO(path);
			invalid = j;
			invalid["materials"][0]["sprite"] = path;
			CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
			invalid = j;
			invalid["compiled_pack"] = path + "/atlas.json";
			CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		}
		// Reject parent segments, not harmless filename substrings.
		invalid = j;
		invalid["materials"][0]["sprite"] = "data/gfx/ice..cracked";
		CHECK_NOTHROW(TerrainVisual::Catalog::parse(invalid));
		auto reordered = j;
		std::reverse(reordered["materials"].begin(), reordered["materials"].end());
		const auto a = TerrainVisual::Catalog::parse(j),
				   b = TerrainVisual::Catalog::parse(reordered);
		for (int x = 0; x < 128; ++x)
			CHECK(a.frame(a.find("ice"), x, 7, 0) == b.frame(b.find("ice"), x, 7, 0));
	}
	TEST_CASE("border classes choose every contact from material semantics")
	{
		glob2test::HeadlessGlobals globals;
		const auto c = catalog();
		REQUIRE(c.version == 4);
		CHECK(c.treatments.empty());
		const auto profile = [&](const char *a, const char *b)
		{ return c.profiles[c.profileFor(c.find(a), c.find(b))].key; };
		// Pooled liquids meet smoothly; land draws its own coast against them.
		CHECK(profile("water", "deep_water") == "still");
		CHECK(profile("deep_water", "dark_water") == "still");
		CHECK(profile("water", "grass") == "shore");
		CHECK(profile("dark_water", "moss") == "shore");
		CHECK(profile("water", "sand") == "beach");
		CHECK(profile("deep_water", "boulders") == "rock");
		CHECK(profile("ice", "water") == "frozen");
		// Flowing lava overruns natural ground and water alike.
		for (const auto *other : {"grass", "boulders", "ridge_rock", "shale", "sand", "mud", "water"})
			CHECK(profile("lava", other) == "molten");
		// Built surfaces keep their own straight edges against everything natural.
		for (const auto *other : {"grass", "boulders", "scree", "mud", "heath", "lava", "water"})
		{
			CHECK(profile("road", other) == "paved");
			CHECK(profile("brick_path", other) == "paved");
			CHECK(profile("boardwalk", other) == "crisp");
		}
		CHECK(profile("road", "boardwalk") == "crisp");
		CHECK(profile("void_hole", "road") == "crisp");
		CHECK(profile("chasm", "lava") == "crisp");
		CHECK(profile("hedge", "void_hole") == "canopy");
		CHECK(profile("thicket", "water") == "canopy");
		// Natural ground: resistance ranks rock over ice over soil, brush and turf.
		CHECK(profile("boulders", "mud") == "rock");
		CHECK(profile("dirt", "grass") == "soil");
		CHECK(profile("deep_snow", "ice") == "frozen");
		CHECK(profile("sand", "grass") == "turf"); // Rooted turf holds its edge on loose sand.
		CHECK(profile("heath", "grass") == "brush");
		CHECK(profile("grass", "lush_grass") == "turf");
		for (unsigned a = 0; a < c.materials.size(); ++a)
			for (unsigned b = 0; b < c.materials.size(); ++b)
				if (a != b)
				{
					const auto x = TerrainVisual::MaterialId(a), y = TerrainVisual::MaterialId(b);
					CHECK(c.profileFor(x, y) == c.profileFor(y, x));
					CHECK_FALSE((c.dominates(x, y) && c.dominates(y, x)));
					CHECK(c.shapes(x, y) != c.shapes(y, x));
				}
		// Earlier catalogs keep the roughest-profile rule.
		auto j = shippedCatalog();
		j["version"] = 3;
		j.erase("border_classes");
		for (auto &m : j["materials"])
		{
			m.erase("class");
			m.erase("stones"); // Whole-stone edges are version 4.
		}
		for (auto &p : j["profiles"])
		{
			p.erase("precedence"); p.erase("coast");
			for (const char *field : {"teeth_q8", "teeth_form", "facets", "clasts", "resistance_q8", "cohesion_q8",
					 "grain_q8", "spread_q8"})
				p.erase(field);
			if (p.value("shape", "") != "smooth") continue;
			p.erase("shape"); p.erase("wiggle_q8"); p.erase("wiggle_period"); p.erase("squareness_q8");
			p["roughness_q8"] = 0;
			p["contours_q12"] = nlohmann::json::array({{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}});
		}
		const auto legacy = TerrainVisual::Catalog::parse(j);
		CHECK(legacy.profiles[legacy.profileFor(legacy.find("lava"), legacy.find("boulders"))].key == "rock");
		CHECK(legacy.contextualFor(legacy.find("water"), legacy.find("grass")) == false);
	}
	TEST_CASE("smooth contacts keep their vertices, continue across tiles and do not jag")
	{
		glob2test::HeadlessGlobals globals;
		const auto c = catalog();
		const auto water = c.find("water"), deep = c.find("deep_water"), dark = c.find("dark_water"),
				   grass = c.find("grass");
		constexpr int size = 16;
		// A seeded mix of liquids with a grass shore in one corner, so tiles
		// cover full smooth shares, mixed shares and rough cells.
		std::array<TerrainVisual::MaterialId, size * size> field{};
		std::uint32_t state = 12345;
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x)
			{
				state = state * 1664525u + 1013904223u;
				const unsigned pick = state >> 29;
				field[y * size + x] = x < 4 && y < 4 ? grass : pick < 4 ? water : pick < 7 ? deep : dark;
			}
		const auto at = [&](int x, int y) { return field[((y + size) % size) * size + (x + size) % size]; };
		std::vector<TerrainVisual::PreparedCoverage> tiles;
		tiles.reserve(size * size);
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x)
				tiles.emplace_back(c, contextualRecipe(x, y, size, at));
		const auto weightOf = [](const TerrainVisual::Coverage &coverage, TerrainVisual::MaterialId material)
		{
			unsigned total = 0;
			for (unsigned k = 0; k < 4; ++k)
				if (coverage.material[k] == material) total += coverage.weight[k];
			return total;
		};
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x)
			{
				const auto &tile = tiles[y * size + x];
				// Every vertex keeps its own material, wiggle and saddles included.
				const int corner[4][2] = {{0, 0}, {8192, 0}, {0, 8192}, {8192, 8192}};
				for (unsigned k = 0; k < 4; ++k)
				{
					const auto own = at(x + int(k & 1), y + int(k >> 1));
					CHECK(weightOf(tile.at(corner[k][0], corner[k][1]), own) > 32768);
				}
				// Right and bottom edges match the neighbouring tile's samples.
				const auto &right = tiles[y * size + (x + 1) % size], &below = tiles[((y + 1) % size) * size + x];
				for (int s = 0; s <= 8192; s += 512)
					for (const auto material : {water, deep, dark, grass})
					{
						INFO("tile " << x << "," << y << " offset " << s);
						CHECK(std::abs(int(weightOf(tile.at(8192, s), material)) - int(weightOf(right.at(0, s), material))) <= 64);
						CHECK(std::abs(int(weightOf(tile.at(s, 8192), material)) - int(weightOf(below.at(s, 0), material))) <= 64);
					}
			}
		// A straight water/deep shore stays a single smooth line: its crossing
		// moves by a fraction of a pixel per pixel row, never in steps.
		const auto straight = [&](int x, int) { return x < 8 ? water : deep; };
		std::vector<float> crossing;
		for (int y = 0; y < size; ++y)
		{
			std::vector<TerrainVisual::PreparedCoverage> row;
			for (int x = 6; x <= 8; ++x) row.emplace_back(c, contextualRecipe(x, y, size, straight));
			for (int py = 128; py < 8192; py += 256)
			{
				float position = -1;
				for (int sx = 0; sx < 3 * 8192 && position < 0; sx += 32)
					if (weightOf(row[sx / 8192].at(sx % 8192, py), water) < 32768)
						position = sx / 256.f;
				REQUIRE(position >= 0);
				crossing.push_back(position);
			}
		}
		float steepest = 0, spread = 0;
		for (unsigned i = 1; i < crossing.size(); ++i)
			steepest = std::max(steepest, std::abs(crossing[i] - crossing[i - 1]));
		const auto [low, high] = std::minmax_element(crossing.begin(), crossing.end());
		spread = *high - *low;
		CHECK(steepest < 1.f);
		CHECK(spread > 1.f);  // It meanders...
		CHECK(spread < 12.f); // ...within the profile's wiggle.
		// Squareness: a 2x2 road block keeps a crisp corner where a round smooth
		// profile cuts it with an arc. Sample just inside the corner.
		const auto road = c.find("road");
		auto round = c;
		round.profiles[round.materials[road].profile].squareness = 0;
		REQUIRE(c.profiles[c.materials[road].profile].squareness >= 192);
		const auto block = [&](int x, int y) { return x >= 4 && x <= 5 && y >= 4 && y <= 5 ? road : grass; };
		const auto recipe = contextualRecipe(3, 3, size, block);
		const TerrainVisual::PreparedCoverage square(c, recipe), arc(round, recipe);
		CHECK(weightOf(square.at(19 * 256, 19 * 256), road) > 32768);
		CHECK(weightOf(arc.at(19 * 256, 19 * 256), road) < 32768);
		// A 45-degree staircase of road vertices runs as one straight diagonal
		// rather than square steps: along every pixel row, the road's far edge
		// keeps a constant offset from the diagonal, apart from the meander.
		const auto stairs = [&](int x, int y) { return ((x - y) % size + size) % size < 6 ? road : grass; };
		std::vector<float> offset;
		for (int y = 4; y < 10; ++y)
		{
			std::vector<TerrainVisual::PreparedCoverage> row;
			for (int x = y + 3; x <= y + 7; ++x) row.emplace_back(c, contextualRecipe(x, y, size, stairs));
			for (int py = 128; py < 8192; py += 256)
			{
				float position = -1;
				for (int sx = 0; sx < 5 * 8192 && position < 0; sx += 32)
					if (weightOf(row[sx / 8192].at(sx % 8192, py), road) < 32768)
						position = sx / 256.f;
				REQUIRE(position >= 0);
				offset.push_back(position - py / 256.f);
			}
		}
		const auto [nearest, farthest] = std::minmax_element(offset.begin(), offset.end());
		CHECK(*farthest - *nearest < 4.f);
	}
	TEST_CASE("single vertices own their corner and animation does not reseed variants")
	{
		glob2test::HeadlessGlobals globals;
		auto c = catalog();
		for (auto key : {"water", "sand", "grass", "ice", "road"})
		{
			// A lone vertex of key: within the warp's six pixels of the tile
			// corner, every sample still lies in the corner's uniform patch.
			TerrainVisual::Recipe r;
			r.width = r.height = 16;
			r.corners.fill(c.find(key) == c.find("grass") ? c.find("ice") : c.find("grass"));
			r.corners[0] = c.find(key);
			for (r.y = 0; r.y < r.height; ++r.y)
				for (r.x = 0; r.x < r.width; ++r.x)
				{
					const TerrainVisual::PreparedCoverage prepared(c, r);
					for (int y : {0, 1})
						for (int x : {0, 1})
						{
							const auto result = prepared.at(x * 256, y * 256);
							unsigned weight = 0;
							for (int k = 0; k < 4; ++k)
								if (result.material[k] == c.find(key))
									weight += result.weight[k];
							CHECK(weight == 65536);
						}
				}
		}
		auto &m = c.materials[c.find("ice")];
		m.animationFrames = 3;
		m.animationTicks = 4;
		m.animationStride = 16;
		for (int t = 0; t < 24; ++t)
			CHECK(c.frame(c.find("ice"), 7, 9, t) ==
				  c.frame(c.find("ice"), 7, 9, 0) + (t / 4) % 3 * 16);
	}
}

TEST_CASE("selected terrain preparation bounds pixels and preserves retained snapshots [display]" *
          doctest::test_suite("TerrainMaterials"))
{
    glob2test::HeadlessGlobals globals({.display = true});
    globals->fileManager->addDir(glob2test::sourceRoot().string());
    TerrainVisual::Compositor compositor(catalog());
    const auto &c = compositor.catalog();
    const auto grass = c.find("grass"), water = c.find("water");
    const auto uniform = [&](TerrainVisual::MaterialId id)
    {
        return contextualRecipe(3, 7, 16, [=](int, int) { return id; });
    };
    const std::array grassRecipes{uniform(grass)}, waterRecipes{uniform(water)};
    const auto retainedPixels = [](const auto &snapshot)
    {
        std::size_t pixels = 0, variants = 0;
        for (const auto &material : snapshot->textures)
            for (const auto &texture : material)
                if (texture.pixels && !texture.pixels->empty())
                {
                    pixels += texture.pixels->size();
                    ++variants;
                }
        return std::pair{pixels, variants};
    };
    for (bool hd : {false, true})
    {
        INFO("hd=" << hd);
        const auto idleWaterRevision = compositor.textureRevision(water);
        compositor.prepareForRecipes(hd, 0, grassRecipes);
        const auto grassView = compositor.preparedMaterial(grassRecipes[0], grass);
        REQUIRE(grassView.size > 0);
        auto retained = compositor.cpuMaterials(std::array{grass});
        const auto [pixelCount, variantCount] = retainedPixels(retained);
        CHECK(variantCount == 1);
        CHECK(pixelCount == std::size_t(grassView.size * grassView.size));
        CHECK(retained->included[grass]);
        CHECK_FALSE(retained->included[water]);
        auto adjacentGrass = grassRecipes[0];
        ++adjacentGrass.x;
        const std::array twoGrassRecipes{grassRecipes[0], adjacentGrass};
        compositor.prepareForRecipes(hd, 0, twoGrassRecipes);
        const auto twoGrass = compositor.cpuMaterials(std::array{grass});
        const auto [twoPixelCount, twoVariantCount] = retainedPixels(twoGrass);
        CHECK(twoVariantCount == 2);
        CHECK(twoPixelCount == pixelCount * 2);
        const auto batch = compositor.cpuMaterialsForRecipes(grassRecipes);
        const auto [batchPixels, batchVariants] = retainedPixels(batch);
        CHECK(batchVariants == 1);
        CHECK(batchPixels == pixelCount);
        CHECK(batch->bytes == compositor.cpuMaterialBytesForRecipes(grassRecipes));
        CHECK(batch->bytes < twoGrass->bytes);
        CHECK(compositor.preparedMaterial(adjacentGrass, grass).size > 0);
        const auto hidden = compositor.cpuMaterialsForRecipes({});
        CHECK(retainedPixels(hidden).second == 0);
        CHECK(twoGrass->bytes > retained->bytes);
        CHECK(twoGrass->bytes <= retained->bytes +
              pixelCount * sizeof(TerrainVisual::Compositor::MaterialPixels::value_type) + 4096);
        compositor.prepareForRecipes(hd, 0, grassRecipes);
        const auto selectedGrassRevision = compositor.textureRevision(grass);
        const auto original = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
            SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        const auto actual = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
            SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        REQUIRE(original);
        REQUIRE(actual);
        retained->compose(grassRecipes[0], original.get(), 1);
        compositor.prepareForRecipes(hd, 5, grassRecipes);
        CHECK(compositor.textureRevision(grass) == selectedGrassRevision);
        CHECK(compositor.textureRevision(water) == idleWaterRevision);

        compositor.prepareForRecipes(hd, 0, waterRecipes);
        const auto phase0View = compositor.preparedMaterial(waterRecipes[0], water);
        REQUIRE_FALSE(phase0View.pixels.empty());
        const TerrainVisual::Compositor::MaterialPixels phase0(phase0View.pixels.begin(), phase0View.pixels.end());
        auto adjacentWater = waterRecipes[0];
        ++adjacentWater.x;
        const std::array twoWaterRecipes{waterRecipes[0], adjacentWater};
        compositor.prepareForRecipes(hd, 0, twoWaterRecipes);
        CHECK_FALSE(compositor.preparedMaterial(adjacentWater, water).pixels.empty());
        const auto unchangedWater = compositor.preparedMaterial(waterRecipes[0], water);
        CHECK(std::equal(phase0.begin(), phase0.end(), unchangedWater.pixels.begin(), unchangedWater.pixels.end()));
        compositor.prepareForRecipes(hd, 6, waterRecipes);
        const auto phase1View = compositor.preparedMaterial(waterRecipes[0], water);
        const TerrainVisual::Compositor::MaterialPixels phase1(phase1View.pixels.begin(), phase1View.pixels.end());
        compositor.prepareForRecipes(hd, 5, waterRecipes);
        const auto blended = compositor.preparedMaterial(waterRecipes[0], water);
        REQUIRE(blended.pixels.size() == phase0.size());
        REQUIRE(blended.pixels.size() == phase1.size());
        const unsigned weightB = (5u * 65536 + 3) / 6;
        for (std::size_t p = 0; p < blended.pixels.size(); ++p)
        {
            REQUIRE(phase0[p][3] == 255);
            REQUIRE(phase1[p][3] == 255);
            for (unsigned channel = 0; channel < 3; ++channel)
                CHECK(blended.pixels[p][channel] ==
                      (phase0[p][channel] * (65536 - weightB) + phase1[p][channel] * weightB + 32768) >> 16);
        }
        auto selected = compositor.cpuMaterials();
        const auto [selectedPixels, selectedVariants] = retainedPixels(selected);
        CHECK(selectedVariants == 1);
        CHECK(selectedPixels == std::size_t(blended.size * blended.size));
        // The byte admission includes catalog metadata, but its live pixel payload
        // is one chosen variant rather than the complete 256-variant bank.
        CHECK(selected->bytes >= selectedPixels * sizeof(TerrainVisual::Compositor::MaterialPixels::value_type));
        retained->compose(grassRecipes[0], actual.get(), 1);
        for (int y = 0; y < 32; ++y)
            CHECK(std::memcmp(static_cast<Uint8 *>(actual->pixels) + y * actual->pitch,
                              static_cast<Uint8 *>(original->pixels) + y * original->pitch, 32 * 4) == 0);
        // Evict water before the next HD mode so its idle revision is comparable.
        compositor.prepareForRecipes(hd, 5, grassRecipes);
    }
}

TEST_CASE("owned prepared materials survive animation and share unchanged revisions [display]" *
          doctest::test_suite("TerrainMaterials"))
{
    glob2test::HeadlessGlobals globals({.display = true});
    TerrainVisual::Compositor compositor(catalog());
    compositor.prepare(false, 0);
    auto retained = compositor.cpuMaterials();
    const auto c = compositor.catalog();
    auto recipe = contextualRecipe(3, 7, 16, [&](int x, int y)
                                   { return (x + y) % 3 ? c.find("sand") : c.find("water"); });
    for (int samplingScale : {1,2,4}) {
        compositor.prepare(false,0);
    auto original = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
        SDL_CreateSurface(32*samplingScale, 32*samplingScale, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
    auto actual = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
        SDL_CreateSurface(32*samplingScale, 32*samplingScale, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
    REQUIRE(original);
    REQUIRE(actual);
    compositor.compose(recipe, original.get(), 0, 0, samplingScale);
    compositor.prepare(false, 5);
    auto changed = compositor.cpuMaterials();
    CHECK(changed != retained);
    CHECK(changed->textures[c.find("sand")][0].pixels ==
          retained->textures[c.find("sand")][0].pixels);
    ComputeExecutor executor;
    executor.configure(4);
    auto batch = executor.submitPresentationBatch(1, {2}, [retained, recipe, &actual, samplingScale](auto)
                                                  { retained->compose(recipe, actual.get(), samplingScale); });
    executor.finishPresentationBatch(batch);
    for (int y = 0; y < 32*samplingScale; ++y)
        CHECK(std::memcmp(static_cast<Uint8 *>(actual->pixels) + y * actual->pitch,
                          static_cast<Uint8 *>(original->pixels) + y * original->pitch,
                          32*samplingScale*4) == 0);
    }
    // Expiring the last publication must not retain unused immutable buffers.
    std::weak_ptr<const TerrainVisual::Compositor::MaterialPixels> released =
        retained->textures[c.find("sand")][0].pixels;
    std::weak_ptr<const TerrainVisual::Catalog> releasedCatalog = retained->definitions;
    retained.reset();
    CHECK(released.expired());
    CHECK(releasedCatalog.expired());
}
TEST_CASE(
    "CPU terrain groups preserve incremental pages under serial and parallel schedules [display]" *
    doctest::test_suite("TerrainMaterials"))
{
    glob2test::HeadlessGlobals globals({.display = true});
    glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .terrain = SAND, .discovered = true});
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if ((x * 7 + y * 13) % 11 < 5)
                fixture.game.map.setVertexTerrain(x, y, WATER);
    ComputeExecutor executor;
    executor.configure(4);
    GAGCore::PresentationWorkScope scope(executor);
    std::array<SoftwareTerrainCache, 3> caches;
    for (int frame = 0; frame < 6; ++frame)
    {
        if (frame == 2)
            fixture.game.map.setVertexTerrain(31, 9, GRASS);
        if (frame == 4) fixture.game.map.unsetMapDiscovered();
        if (frame == 5) fixture.game.map.setMapDiscovered(2,2,7,9,fixture.team->me);
        SceneMap scene;
        glob2test::observeMap(fixture.game.map, scene);
        std::vector<Uint32> expected;
        for (unsigned mode = 0; mode < 3; ++mode)
        {
            glob2test::ScopedEnvironment execution("GLOB2_RENDER_EXECUTION", mode == 0 ? "immediate"
                                                                             : mode == 1
                                                                                 ? "serial"
                                                                                 : "parallel");
            auto &cache = caches[mode];
            REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, frame == 3 ? 16 : 0, 0,
                                  fixture.team->me, frame < 4, frame));
            globals->gfx->drawFilledRect(0, 0, 640, 480, GAGCore::Color(11, 22, 33));
            cache.draw(*globals->gfx);
            globals->gfx->finishDrawingSprite(globals->terrain, 255);
            const auto *output = globals->gfx->getSDLSurface();
            std::vector<Uint32> current;
            for (int y = 0; y < output->h; ++y)
            {
                const auto *row = reinterpret_cast<const Uint32 *>(
                    static_cast<const Uint8 *>(output->pixels) + y * output->pitch);
                current.insert(current.end(), row, row + output->w);
            }
            if (mode == 0)
                expected = std::move(current);
            else
                CHECK(current == expected);
        }
    }
    CHECK(caches[1].workMetrics.cells > 0);
    CHECK(caches[2].workMetrics.jobs > 0);
    CHECK(caches[2].workMetrics.peakReservedBytes <= 4u * 1024u * 1024u);
}
TEST_CASE("failed CPU terrain groups retire invalid pages and recover on the next prepare [display]" * doctest::test_suite("TerrainMaterials"))
{
    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION","parallel");
    glob2test::HeadlessGlobals globals({.display=true});
    glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.terrain=SAND,.discovered=true});
    fixture.game.map.setVertexTerrain(7,9,WATER);
    SceneMap scene;glob2test::observeMap(fixture.game.map,scene);
    SoftwareTerrainCache cache;
    struct AllocationFailure : GAGCore::PresentationWork {
        void executePresentationJobs(std::size_t count,const std::function<void(std::size_t)>& job) override {
            if(count)job(0);
            throw std::bad_alloc();
        }
    } failure;
    { GAGCore::PresentationWorkScope scope(failure);
        CHECK_FALSE(cache.prepare(scene,*globals->terrain,0,0,15,15,0,0,fixture.team->me,true,0));
    }
    CHECK(cache.bytes()==0);CHECK(cache.maskBytes()==0);
    ComputeExecutor executor;executor.configure(3);GAGCore::PresentationWorkScope scope(executor);
    CHECK(cache.prepare(scene,*globals->terrain,0,0,15,15,0,0,fixture.team->me,true,0));
    CHECK(cache.workMetrics.jobs>0);
}
TEST_CASE("shared reservation pressure preserves serial terrain before readmitting jobs [display]" * doctest::test_suite("TerrainMaterials"))
{
    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION","parallel");
    glob2test::HeadlessGlobals globals({.display=true});
    glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.terrain=SAND,.discovered=true});
    fixture.game.map.setVertexTerrain(7,9,WATER);
    ComputeExecutor executor;executor.configure(4);GAGCore::PresentationWorkScope scope(executor);
    SceneMap scene;glob2test::observeMap(fixture.game.map,scene);
    SoftwareTerrainCache cache;
    GAGCore::RenderWorkReservation occupied;
    REQUIRE(occupied.reserve(GAGCore::RenderWorkReservation::available()));
    REQUIRE(cache.prepare(scene,*globals->terrain,0,0,15,15,0,0,fixture.team->me,true,0));
    CHECK(cache.workMetrics.jobs==0);CHECK(cache.workMetrics.serialPages>0);
    occupied.release();
    REQUIRE(cache.prepare(scene,*globals->terrain,0,0,15,15,0,0,fixture.team->me,true,1));
    CHECK(cache.workMetrics.jobs>0);
}

TEST_CASE("CPU terrain retires coverage when cells leave discovery [display]" *
          doctest::test_suite("TerrainMaterials"))
{
    glob2test::ScopedEnvironment workers("GLOB2_RENDER_WORKERS", "2");
    glob2test::HeadlessGlobals globals({.display = true});
    glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .terrain = SAND, .discovered = true});
    fixture.game.map.setVertexTerrain(3, 3, WATER);
    fixture.game.map.setVertexTerrain(11, 11, WATER);
    ComputeExecutor executor;
    executor.configure(4);
    executor.configureTelemetry(ComputeTelemetryMode::Trace);
    GAGCore::PresentationWorkScope scope(executor);
    std::array<SoftwareTerrainCache, 3> caches;
    for (int phase = 0; phase < 3; ++phase)
    {
        if (phase == 1)
        {
            fixture.game.map.unsetMapDiscovered();
            fixture.game.map.setMapDiscovered(9, 9, 15, 15, fixture.team->me);
            // Recompose a discovered cell in the same group as hidden cells;
            // this exercises mask retirement during an admitted CPU batch.
            fixture.game.map.setVertexTerrain(12, 12, GRASS);
        }
        if (phase == 2) fixture.game.map.unsetMapDiscovered();
        SceneMap scene;
        glob2test::observeMap(fixture.game.map, scene);
        for (unsigned mode = 0; mode < caches.size(); ++mode)
        {
            glob2test::ScopedEnvironment execution("GLOB2_RENDER_EXECUTION",
                                                   mode == 0 ? "immediate" : mode == 1 ? "serial" : "parallel");
            const auto previousBatches = caches[mode].workMetrics.batches;
            REQUIRE(caches[mode].prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0,
                                        fixture.team->me, phase == 0, 0));
            if (phase == 0) CHECK(caches[mode].maskBytes() > 0);
            // Pixel/coverage parity alone could pass through a serial fallback.
            // Require the partial-fog transition to use the CPU grouping path.
            if (phase == 1 && mode != 0)
                CHECK(caches[mode].workMetrics.batches > previousBatches);
            if (phase == 2) CHECK(caches[mode].maskBytes() == 0);
            CHECK(caches[mode].maskBytes() == caches[0].maskBytes());
        }
#if !defined(__EMSCRIPTEN__)
        // Native opt-in mode must reach the executor, rather than merely
        // constructing private results and processing them on the owner.
        if (phase == 0) CHECK(executor.graphicsMetrics().parallelBatches > 0);
#endif
    }
}

TEST_CASE("sparse CPU terrain edits snapshot only changed materials [display]" *
          doctest::test_suite("TerrainMaterials"))
{
    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "parallel");
    glob2test::HeadlessGlobals globals({.display = true});
    glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .terrain = SAND, .discovered = true});
    fixture.game.map.setVertexTerrain(3, 3, WATER);
    fixture.game.map.setVertexTerrain(5, 5, ICE);
    ComputeExecutor executor;
    executor.configure(4);
    GAGCore::PresentationWorkScope scope(executor);
    SoftwareTerrainCache cache;
    SceneMap scene;
    glob2test::observeMap(fixture.game.map, scene);
    REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0, fixture.team->me, true, 0));
    const auto originalBytes = cache.bytes();
    const auto originalCells = cache.workMetrics.cells;
    const auto originalBatches = cache.workMetrics.batches;

    // Water and ice variants are unchanged elsewhere in this page. A distant local
    // edit should replace the retained snapshot with sand/grass alone.
    fixture.game.map.setVertexTerrain(13, 13, GRASS);
    glob2test::observeMap(fixture.game.map, scene);
    REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0, fixture.team->me, true, 0));
    CHECK(cache.workMetrics.batches > originalBatches);
    CHECK(cache.workMetrics.cells - originalCells < SoftwareTerrainCache::ChunkTiles * SoftwareTerrainCache::ChunkTiles);
    CHECK(cache.bytes() < originalBytes);
}

TEST_CASE("partial direct CPU terrain writes stay unpublished after a failed rebuild [display]" *
          doctest::test_suite("TerrainMaterials"))
{
    glob2test::ScopedEnvironment mode("GLOB2_RENDER_EXECUTION", "parallel");
    glob2test::HeadlessGlobals globals({.display = true});
    glob2test::HeadlessGame fixture({.wDec = 5, .hDec = 5, .terrain = SAND, .discovered = true});
    fixture.game.map.setVertexTerrain(7, 9, WATER);
    SceneMap scene;
    glob2test::observeMap(fixture.game.map, scene);
    SoftwareTerrainCache cache;
    ComputeExecutor executor;
    executor.configure(4);
    {
        GAGCore::PresentationWorkScope scope(executor);
        REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0,
                              fixture.team->me, true, 0));
    }
    const auto originalBatches = cache.workMetrics.batches;
    const auto originalMasks = cache.maskBytes();
    // Change every cell and retain animated coverage, so the first group
    // installs new masks/opacity metadata before the second group fails.
    for (int y = 0; y <= SoftwareTerrainCache::ChunkTiles; ++y)
        for (int x = 0; x <= SoftwareTerrainCache::ChunkTiles; ++x)
            fixture.game.map.setVertexTerrain(x, y, (x + y) % 2 ? GRASS : WATER);
    glob2test::observeMap(fixture.game.map, scene);
    struct FailedRebuild : GAGCore::PresentationWork
    {
        unsigned completedGroups = 0, failedGroups = 0;
        void executePresentationJobs(std::size_t count,
                                     const std::function<void(std::size_t)> &job) override
        {
            REQUIRE(count > 0);
            if (!completedGroups)
            {
                for (std::size_t i = 0; i < count; ++i) job(i);
                ++completedGroups;
                return; // The owner installs this group's retained metadata.
            }
            job(0); // Partially mutate the second unpublished group.
            ++failedGroups;
            throw std::runtime_error("Injected direct terrain rebuild failure");
        }
    } failure;
    const auto available = GAGCore::RenderWorkReservation::available();
    {
        GAGCore::PresentationWorkScope scope(failure);
        CHECK_THROWS_AS(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0,
                                     fixture.team->me, true, 0), std::runtime_error);
    }
    CHECK(failure.completedGroups == 1);
    CHECK(failure.failedGroups == 1);
    CHECK(cache.workMetrics.batches == originalBatches + 1);
    CHECK(cache.maskBytes() > originalMasks);
    CHECK(cache.bytes() > 0); // Non-allocation failures retain the invalid cache page.
    CHECK(GAGCore::RenderWorkReservation::available() == available);

    auto pixels = [&]
    {
        const auto *surface = globals->gfx->getSDLSurface();
        std::vector<Uint32> result;
        for (int y = 0; y < surface->h; ++y)
        {
            const auto *row = reinterpret_cast<const Uint32 *>(
                static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch);
            result.insert(result.end(), row, row + surface->w);
        }
        return result;
    };
    globals->gfx->drawFilledRect(0, 0, globals->gfx->getW(), globals->gfx->getH(),
                                GAGCore::Color(11, 22, 33));
    const auto untouched = pixels();
    cache.draw(*globals->gfx);
    CHECK(pixels() == untouched);

    // Retained partial pixels must be overwritten by a complete rebuild, and
    // the owner's surface lock must have unwound before that recovery begins.
    {
        GAGCore::PresentationWorkScope scope(executor);
        REQUIRE(cache.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0,
                              fixture.team->me, true, 0));
    }
    cache.draw(*globals->gfx);
    const auto recovered = pixels();
    SoftwareTerrainCache reference;
    glob2test::ScopedEnvironment immediate("GLOB2_RENDER_EXECUTION", "immediate");
    REQUIRE(reference.prepare(scene, *globals->terrain, 0, 0, 15, 15, 0, 0,
                              fixture.team->me, true, 0));
    globals->gfx->drawFilledRect(0, 0, globals->gfx->getW(), globals->gfx->getH(),
                                GAGCore::Color(11, 22, 33));
    reference.draw(*globals->gfx);
    CHECK(pixels() == recovered);
}
