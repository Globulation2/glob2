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

namespace
{
TerrainVisual::Catalog catalog()
{
	return TerrainVisual::loadCatalog();
}
} // namespace
TEST_SUITE("TerrainMaterials")
{
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
		ice.animationTicks = 1;
		globals->terrainCompositor_ =
			std::make_unique<TerrainVisual::Compositor>(std::move(definitions));
		glob2test::HeadlessGame fixture({.wDec = 6, .hDec = 5, .discovered = true});
		for (int y = 0; y < 32; ++y)
			for (int x = 32; x < 48; ++x)
				fixture.game.map.setCellTerrain(x, y, ICE);
		SceneMap scene;
		scene.extract(fixture.game.map);
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
		recipe.samples.fill(definitions.find("ice"));
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
		for (int i = 5; i < 64; ++i)
		{
			auto m = j["materials"][2];
			m["key"] = "fixture-" + std::to_string(i);
			j["materials"].push_back(m);
		}
		TerrainVisual::Compositor compositor(TerrainVisual::Catalog::parse(j));
		compositor.prepare(false, 0);
		TerrainVisual::Recipe r;
		r.width = r.height = 16;
		for (int i = 0; i < 16; ++i)
			r.samples[i] = 60 + (i % 4);
		GAGCore::DrawableSurface result(32, 32);
		compositor.compose(r, result.getSDLSurface(), 0, 0, 1);
		CHECK(result.hasOpaquePixels());
	}

	TEST_CASE("all legacy shore groups retain their corner orientation")
	{
		constexpr unsigned masks[] = {8, 4, 1, 2, 3, 12, 5, 10, 7, 11, 14, 13, 6, 9};
		for (unsigned group = 0; group < 14; ++group)
			for (unsigned variant = 0; variant < 8; ++variant)
			{
				const auto a = TerrainVisual::legacyCorners(16 + group * 8 + variant);
				const auto b = TerrainVisual::legacyCorners(144 + group * 8 + variant);
				for (int k = 0; k < 4; ++k)
				{
					CHECK(a[k] == ((masks[group] >> k & 1) ? 2 : 1));
					CHECK(b[k] == (((masks[group] ^ (group >= 12 ? 15u : 0u)) >> k & 1) ? 1 : 0));
				}
			}
	}
	TEST_CASE("legacy decoder inverts the engine lookup for both compatibility profiles")
	{
		glob2test::HeadlessGlobals globals;
		struct LegacyMap : Map
		{
			using Map::lookup;
		} map;
		for (unsigned low : {0u, 1u})
			for (unsigned mask = 0; mask < 16; ++mask)
			{
				std::array<unsigned, 4> corners{};
				for (int k = 0; k < 4; ++k)
					corners[k] = low + ((mask >> k) & 1);
				for (int variation = 0; variation < 16; ++variation)
				{
					const auto frame = map.lookup(corners[0], corners[1], corners[2], corners[3]);
					CHECK(TerrainVisual::legacyCorners(frame) == corners);
				}
			}
	}
	TEST_CASE("prepared coverage preserves native and HD contour geometry")
	{
		glob2test::HeadlessGlobals globals;
		const auto definitions = catalog();
		std::uint64_t digest = 14695981039346656037ull;
		for (unsigned configuration = 0; configuration < 256; ++configuration)
		{
			TerrainVisual::Recipe recipe;
			recipe.width = recipe.height = 16;
			recipe.x = 15;
			recipe.y = 9;
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
					recipe.samples[y * 4 + x] =
						(configuration >> (2 * ((x & 1) + 2 * (y & 1)))) & 3;
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
		// Captured from the original per-pixel resolver before hoisting patch
		// invariants. Intentional catalog contour changes require visual review
		// and an updated digest, not just a matching partition sum.
		CHECK(digest == 18185691832014944171ull);
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
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
					r.samples[y * 4 + x] = (configuration >> (2 * ((x & 1) + 2 * (y & 1)))) & 3;
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
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 4; ++x)
			{
				a.samples[y * 4 + x] = (x + y) % 5;
				b.samples[y * 4 + x] = (x + y + 2) % 5;
			}
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
		for (auto &profile : c.profiles)
			profile.roughness = 0;
		TerrainVisual::Recipe r;
		r.width = r.height = 16;
		r.samples.fill(c.find("grass"));
		r.samples[5] = r.samples[10] = c.find("road");
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
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 4; ++x)
			{
				a.samples[y * 4 + x] = (x + y) % 5;
				b.samples[y * 4 + x] = (x + y + 2) % 5;
			}
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

	TEST_CASE("catalog rejects corrupt references and grows independently of gameplay IDs")
	{
		glob2test::HeadlessGlobals globals;
		std::ifstream input(glob2test::sourceRoot() / "data/terrain/tileset.json");
		auto j = nlohmann::json::parse(input);
		for (int i = 0; i < 59; ++i)
		{
			auto m = j["materials"][2];
			m["key"] = "fixture-" + std::to_string(i);
			j["materials"].push_back(m);
		}
		CHECK(TerrainVisual::Catalog::parse(j).materials.size() == 64);
		auto invalid = j;
		invalid["materials"][0]["variants"][0]["weight"] = 0;
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		invalid = j;
		invalid["profiles"][0]["contours_q12"][0][0] = 1;
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		invalid = j;
		invalid["bindings"]["ice"] = "absent";
		CHECK_THROWS(TerrainVisual::Catalog::parse(invalid));
		const std::vector<std::pair<nlohmann::json::json_pointer, nlohmann::json>> malformed = {
			{nlohmann::json::json_pointer("/version"), true},
			{nlohmann::json::json_pointer("/version"), 1.0},
			{nlohmann::json::json_pointer("/compiled_pack"), 0},
			{nlohmann::json::json_pointer("/compiled_pack"), "data/terrain/wrong.json"},
			{nlohmann::json::json_pointer("/compiled_pack"), "data/atlas.json"},
			{nlohmann::json::json_pointer("/profiles"), nlohmann::json::object()},
			{nlohmann::json::json_pointer("/materials/0/ocean"), 1},
			{nlohmann::json::json_pointer("/materials/0/preview"), {1, 2, true}},
			{nlohmann::json::json_pointer("/materials/0/variants/0/weight"), 1.5},
			{nlohmann::json::json_pointer("/materials/0/animation_frames"), 2},
			{nlohmann::json::json_pointer("/materials/1/backdrop"),
			 {{"sprite", "data/gfx/terrain"}, {"ticks", 1.5}}},
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
	TEST_CASE("single cells retain their center and animation does not reseed variants")
	{
		glob2test::HeadlessGlobals globals;
		auto c = catalog();
		for (auto key : {"ice", "road"})
		{
			TerrainVisual::Recipe r;
			r.width = r.height = 16;
			r.x = r.y = 4;
			r.samples.fill(c.find("grass"));
			for (auto index : {5, 6, 9, 10})
				r.samples[index] = c.find(key);
			const auto result = coverage(c, r, 16 * 256, 16 * 256);
			CHECK(result.material[0] == c.find(key));
			CHECK(result.weight[0] == 65536);
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
