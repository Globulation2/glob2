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
					for (const char *field : {"feather_q8", "amplitude_q8", "speckle_q8", "bridge_q8"})
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
