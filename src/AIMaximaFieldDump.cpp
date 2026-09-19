// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIMaximaFieldDump.h"

#include "MapRender.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace AIMaximaFieldDump
{
namespace
{
struct Named
{
	const char *name;
	int red, green, blue;
};
// One colour each, so several fields from the same tick stay distinguishable
// when they are flicked through as a series.
const Named kFields[] = {{"threat", 255, 40, 40},
						 {"protectedness", 40, 120, 255},
						 {"foodOpportunity", 40, 220, 120},
						 {"farmCapacity", 220, 200, 40},
						 {"protectedYield", 220, 120, 220}};

int value(const AIMaximaPlacement::WorldTile &tile, const std::string &field)
{
	if (field == "threat")
		return tile.threat;
	if (field == "protectedness")
		return tile.protectedness;
	if (field == "foodOpportunity")
		return int(tile.foodOpportunity);
	if (field == "farmCapacity")
		return int(tile.farmCapacity);
	return int(tile.protectedYield);
}
} // namespace

int maybeWrite(const AIMaximaPlacement::WorldState &world, Game &game, int team, int tick,
			   int lastTick)
{
	static const char *directory = std::getenv("GLOB2_FIELD_DIR");
	static const int interval = []() {
		const char *value = std::getenv("GLOB2_FIELD_INTERVAL");
		return value ? std::atoi(value) : 2500;
	}();
	static const bool renderPng = std::getenv("GLOB2_FIELD_RENDER") != nullptr;
	if (!directory || interval <= 0)
		return lastTick;
	// The placement pass runs on its own cadence, so an exact multiple of the
	// interval would almost never coincide with it: dump on elapsed time since
	// the last one instead.
	if (lastTick >= 0 && tick - lastTick < interval)
		return lastTick;
	const int width = world.width, height = world.height;
	if (width <= 0 || height <= 0 || int(world.tiles.size()) < width * height)
		return lastTick;
	std::filesystem::create_directories(directory);
	for (const Named &named : kFields)
	{
		std::vector<int> values;
		values.reserve(std::size_t(width) * height);
		for (int i = 0; i < width * height; ++i)
			values.push_back(value(world.tiles[i], named.name));
		std::ostringstream stem;
		stem << directory << "/tick-" << std::setfill('0') << std::setw(7) << tick << ".team"
			 << team << "." << named.name;
		std::ofstream out(stem.str() + ".field");
		out << width << " " << height << "\n";
		for (std::size_t i = 0; i < values.size(); ++i)
			out << values[i] << ((i + 1) % width ? ' ' : '\n');
		out.close();
		if (!renderPng)
			continue;
		MapRender::Field painted;
		painted.width = width;
		painted.height = height;
		painted.values = values;
		painted.red = named.red;
		painted.green = named.green;
		painted.blue = named.blue;
		try
		{
			MapRender::toPng(game, stem.str() + ".png", 2048, &painted);
		}
		catch (const std::exception &error)
		{
			std::cerr << "GLOB2_FIELD_RENDER: " << error.what() << std::endl;
		}
	}
	return tick;
}

} // namespace AIMaximaFieldDump
