// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "TerrainMaterials.h"
#include "TerrainCompositor.h"
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
	return TerrainVisual::Compositor::loadCatalog();
}
} // namespace
TEST_SUITE("TerrainMaterials")
{
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
		auto edited = j;
		edited["materials"][0]["variants"][0]["weight"] = 2;
		CHECK_FALSE(TerrainVisual::CompiledPack::load(TerrainVisual::Catalog::parse(edited)));
		packed["frames"][0]["rect"] = {124, 4, 32, 32};
		write();
		CHECK_THROWS(TerrainVisual::CompiledPack::load(c));
		std::filesystem::remove(dir / "atlas.json");
		CHECK_FALSE(TerrainVisual::CompiledPack::load(c));
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
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
				{
					const auto a = TerrainVisual::coverage(c, r, x * 256 + 128, y * 256 + 128);
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
