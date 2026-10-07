#include "TerrainMaterials.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main()
	{
		
		for (bool legacy : {false, true})
		{
			std::ifstream current("data/terrain/tileset.json"); auto definitions = TerrainVisual::Catalog::parse(nlohmann::json::parse(current));
			if (legacy)
			{
				std::ifstream input("data/terrain/tileset.json");
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
			// Fingerprint the reviewed native/HD geometry. Intentional contour changes
			// require a rendered comparison and an updated digest, not just a
			// matching partition sum.
			std::cout << legacy << " " << digest << std::endl; if(digest != (legacy ? 18185691832014944171ull : 1965875410804105497ull)) return 1;
		}
	}