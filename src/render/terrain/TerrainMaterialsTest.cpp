// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "TerrainMaterials.h"
#include "TerrainCompositor.h"
#include "TerrainCatalogIO.h"
#include "render/SoftwareTerrainCache.h"
#include "render/scene/SceneMap.h"
#include "TerrainCompiledPack.h"
#include <FileManager.h>
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

	TEST_CASE("natural border visual fixture and cold composition timings [display] [artifacts]")
	{
		glob2test::HeadlessGlobals globals({.display = true, .width = 768, .height = 768});
		auto current = catalog(); current.compiledPack.clear();
		auto previous = current;
		for (auto &profile : previous.profiles) profile.contextual = false;
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

	TEST_CASE("terrain crossfade preserves endpoints midpoint loop and paused revisions [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto definitions = catalog();
		definitions.compiledPack.clear();
		const auto id = definitions.find("ice");
		auto &ice = definitions.materials[id];
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
		const auto revision = compositor.materialRevision(id);
		const auto bytes = compositor.sourceBytes();
		CHECK(pixel(2) == 0xff780078u);
		CHECK(compositor.materialRevision(id) == revision);
		CHECK(pixel(4) == 0xff0000f0u);
		CHECK(pixel(6) == 0xff780078u);
		CHECK(pixel(8) == 0xfff00000u);
		CHECK(compositor.sourceBytes() == bytes);
		// Jump by a whole phase with the same blend fraction: swapped endpoints
		// must still invalidate composed terrain pages.
		const auto beforeJump = compositor.materialRevision(id);
		CHECK(pixel(12) == 0xff0000f0u);
		CHECK(compositor.materialRevision(id) == beforeJump + 1);
		b->drawFilledRect(0, 0, 32, 32, 0, 240, 0);
		CHECK(pixel(12) == 0xff00f000u);
		REQUIRE(SDL_FillSurfaceRect(b->getSDLSurface(), nullptr, 0x0000ffffu));
		b->markPixelsChanged();
		CHECK(pixel(2) == 0x80f00000u); // Transparent cyan contributes no colour.
	}
	TEST_CASE("animation invalidates only pages using that material [display]")
	{
		glob2test::HeadlessGlobals globals({.display = true});
		auto definitions = catalog();
		definitions.compiledPack.clear(); // Synthetic animation has no compiled artifact.
		auto &ice = definitions.materials[definitions.find("ice")];
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
	TEST_CASE("unequal variant weights affect deterministic texture selection")
	{
		glob2test::HeadlessGlobals globals;
		auto definitions = catalog();
		const auto id = definitions.find("ice");
		auto &material = definitions.materials[id];
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
		std::ifstream input(glob2test::sourceRoot() / "data/terrain/tileset.json");
		auto j = nlohmann::json::parse(input);
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
		std::ifstream input(glob2test::sourceRoot() / "data/terrain/tileset.json");
		auto document = nlohmann::json::parse(input);
		document["compiled_pack"] = "data/terrain/startup-override/atlas.json";
		for (auto &material : document["materials"])
			if (material["key"] == "ice")
				material["variants"] = {{{"frame", 272}, {"weight", 1}}};
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
		std::ifstream input(glob2test::sourceRoot() / "data/terrain/tileset.json");
		auto j = nlohmann::json::parse(input);
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
								if (other.cast && other.height > self.height && int(mask.margin) < other.castWidth)
									shade = 256 - other.cast * (other.castWidth - int(mask.margin)) / other.castWidth;
								if (other.fringe && int(mask.margin) < other.fringeWidth)
									tint = other.fringe * (other.fringeWidth - int(mask.margin)) / other.fringeWidth;
							}
							const auto channel = [&](int k)
							{
								unsigned value = unsigned(alpha ? rgb[k] / alpha : 0) * shade >> 8;
								return value + (unsigned(other.fringeColor[k]) - value) * tint / 256;
							};
							const Uint32 expected = ((alpha + 32768) / 65536 << 24) | (channel(0) << 16) |
													(channel(1) << 8) | channel(2);
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
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				differentVariants += c.variantIndex(grass, x, y, 1) != c.variantIndex(grass, x, y, 2);
		CHECK(differentVariants > 700); // 15/16 of cells differ between independent seeds.
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
				std::ifstream input(glob2test::sourceRoot() / "data/terrain/tileset.json");
				auto j = nlohmann::json::parse(input);
				j["version"] = 1;
				j.erase("boundary_warp_q8");
				for (auto &material : j["materials"])
					material.erase("seam");
				const int roughness[] = {256, 320, 192};
				for (unsigned i = 0; i < j["profiles"].size(); ++i)
				{
					auto &p = j["profiles"][i];
					for (const char *field : {"feather_q8", "amplitude_q8", "speckle_q8", "bridge_q8", "shape"})
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
			CHECK(digest == (legacy ? 15866489041356360345ull : 14215220908205104757ull));
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
		// One material per new profile: rock, soft, crisp and brush, beside grass.
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
		// Intentional profile changes need a rendered comparison and a new digest.
		CHECK(digest == 11045336540448780839ull);
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
			std::set<int> seen;
			for (int y = 0; y < 64; ++y)
				for (int x = 0; x < 64; ++x)
				{
					auto frame = c.frame(id, x, y, 0);
					seen.insert(frame);
					CHECK(frame == c.frame(id, x, y, 100));
				}
			CHECK(seen.size() == 16);
		}
	}

	TEST_CASE("grid variants repeat one positional block independent of the map seed")
	{
		glob2test::HeadlessGlobals globals;
		const auto c = catalog();
		const auto water = c.find("water");
		REQUIRE(c.materials[water].variantGrid == 4);
		for (std::uint32_t seed : {0u, 1u, 0xdeadbeefu})
			for (int y = -4; y < 12; ++y)
				for (int x = -4; x < 12; ++x)
				{
					INFO(seed << " " << x << " " << y);
					CHECK(c.variantIndex(water, x, y, seed) == unsigned((y & 3) * 4 + (x & 3)));
				}

		std::ifstream input(glob2test::sourceRoot() / "data/terrain/tileset.json");
		const auto j = nlohmann::json::parse(input);
		std::size_t index = 0;
		while (j["materials"][index]["key"] != "water")
			++index;
		auto invalid = j;
		invalid["materials"][index]["variant_grid"] = 3;
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		invalid = j;
		invalid["materials"][index]["variant_grid"] = 8; // 16 variants, not 64.
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		invalid = j;
		invalid["materials"][index].erase("edges");
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
	}

	TEST_CASE("catalog rejects corrupt references and grows independently of gameplay IDs")
	{
		glob2test::HeadlessGlobals globals;
		std::ifstream input(glob2test::sourceRoot() / "data/terrain/tileset.json");
		auto j = nlohmann::json::parse(input);
		for (auto i = j["materials"].size(); i < 64; ++i)
		{
			auto m = j["materials"][2];
			m["key"] = "fixture-" + std::to_string(i);
			j["materials"].push_back(m);
		}
		CHECK(TerrainVisual::Catalog::parse(j).materials.size() == 64);
		auto patchOnly = j;
		for (auto &profile : patchOnly["profiles"]) profile.erase("shape");
		const auto withoutShapes = TerrainVisual::Catalog::parse(patchOnly);
		CHECK(std::none_of(withoutShapes.profiles.begin(), withoutShapes.profiles.end(),
			[](const auto &profile) { return profile.contextual; }));
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
