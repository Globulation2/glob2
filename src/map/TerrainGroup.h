// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainPropertiesLayout.h"
#include <array>
#include <cstdint>

// A terrain group is one gameplay mechanic: every built-in type in a group shares
// this exact property profile and differs only in artwork, name and colours.
// Groups are also the unit the editor palette and the terrain experiments use.
// Adding a mechanic means adding a group here and, if it needs an experiment, one
// ExperimentId in TerrainExperiments.h; adding a look means adding a row to
// TerrainTypeTable.h. This header stays free of ExperimentalFeatures.h so the
// gradient kernels can include it.
enum class TerrainGroup : std::uint8_t
{
	Water,
	Sand,
	Grass,
	Ice,
	Paths,
	Obstacles,
	Ridges,
	Barren,
	Rough,
	Lava,
	Fertile,
	DeepWater,
	Void,
	Count
};
inline constexpr unsigned TERRAIN_GROUP_COUNT = unsigned(TerrainGroup::Count);

struct TerrainGroupDefinition
{
	TerrainGroup group;
	// Stable key for palette actions and documentation; label is a string-table key.
	const char *key, *label;
	bool paletteVisible;
	TerrainProperties properties;
};

inline constexpr std::uint16_t TERRAIN_GRASS_RESOURCES =
	(1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 7);

inline constexpr auto TERRAIN_GROUPS = []
{
	std::array<TerrainGroupDefinition, TERRAIN_GROUP_COUNT> groups{};
	auto define = [&](TerrainGroup group, const char *key, const char *label, bool visible)
		-> TerrainProperties &
	{
		auto &entry = groups[unsigned(group)];
		entry = {group, key, label, visible, TerrainProperties{}};
		return entry.properties;
	};
	{
		auto &water = define(TerrainGroup::Water, "water", "[terrain group water]", true);
		water.swimmable = true;
		water.resourcesGrow = water.fertilitySource = true;
		water.fertilityQ8 = 256;
		water.allowedResources = (1u << 4); // algae
		water.farmMaterial = materialIndex(MaterialId::Algae);
	}
	{
		auto &sand = define(TerrainGroup::Sand, "sand", "[terrain group sand]", true);
		sand.walkable = sand.shoreline = true;
		sand.inhibitionQ8 = sand.shoreSupportQ8 = 256;
	}
	{
		auto &grass = define(TerrainGroup::Grass, "grass", "[terrain group grass]", true);
		grass.walkable = grass.resourcesGrow = grass.nonGrowingResources = grass.buildable = true;
		grass.allowedResources = TERRAIN_GRASS_RESOURCES;
		grass.farmMaterial = materialIndex(MaterialId::Food);
	}
	{
		auto &ice = define(TerrainGroup::Ice, "ice", "[terrain group ice]", true);
		ice.walkable = true;
		ice.groundSpeedQ8 = 128;
		ice.groundHealthQ8 = -8;
	}
	{
		// Trail, dirt track, boardwalk: fast, buildable, nothing grows.
		auto &paths = define(TerrainGroup::Paths, "paths", "[terrain group paths]", true);
		paths.walkable = paths.buildable = true;
		paths.groundSpeedQ8 = 512;
	}
	{
		// Boulders, hedges, thickets: impassable on the ground, stop projectiles.
		auto &obstacles =
			define(TerrainGroup::Obstacles, "obstacles", "[terrain group obstacles]", true);
		obstacles.projectileBlocks = true;
	}
	{
		// Ridges and outcrops: impassable on the ground, towers shoot over them.
		define(TerrainGroup::Ridges, "ridges", "[terrain group ridges]", true);
	}
	{
		// Dirt, clay, gravel, flower meadow: buildable, inhibits growth like sand,
		// but no shore support so it never acts as a coastline.
		auto &barren = define(TerrainGroup::Barren, "barren", "[terrain group barren]", true);
		barren.walkable = barren.buildable = true;
		barren.inhibitionQ8 = 256;
	}
	{
		// Mud, marsh, deep snow, scree: slow, unbuildable, nothing grows.
		auto &rough = define(TerrainGroup::Rough, "rough", "[terrain group rough]", true);
		rough.walkable = true;
		rough.groundSpeedQ8 = 160;
	}
	{
		// Lava and ember fields: impassable on the ground, fliers burn.
		auto &lava = define(TerrainGroup::Lava, "lava", "[terrain group lava]", true);
		lava.airHealthQ8 = -64;
	}
	{
		// Loam, moss, spring meadow: buildable crop land that irrigates its
		// surroundings more strongly than water does.
		auto &fertile = define(TerrainGroup::Fertile, "fertile", "[terrain group fertile]", true);
		fertile.walkable = fertile.buildable = fertile.resourcesGrow = true;
		fertile.nonGrowingResources = fertile.fertilitySource = true;
		fertile.fertilityQ8 = 768;
		fertile.allowedResources = TERRAIN_GRASS_RESOURCES;
		fertile.farmMaterial = materialIndex(MaterialId::Food);
	}
	{
		// Deep and dark water: slower swimming, no algae, still irrigates.
		auto &deep =
			define(TerrainGroup::DeepWater, "deep-water", "[terrain group deep-water]", true);
		deep.swimmable = deep.fertilitySource = true;
		deep.fertilityQ8 = 256;
		deep.groundSpeedQ8 = 192;
	}
	{
		// Holes and chasms: nothing crosses, not even fliers or projectiles.
		auto &hole = define(TerrainGroup::Void, "void", "[terrain group void]", true);
		hole.flyable = false;
		hole.projectileBlocks = true;
	}
	return groups;
}();

inline constexpr const TerrainGroupDefinition &terrainGroupDefinition(TerrainGroup group)
{
	return TERRAIN_GROUPS[unsigned(group)];
}

static_assert(
	[]
	{
		for (unsigned i = 0; i < TERRAIN_GROUP_COUNT; ++i)
		{
			const auto &g = TERRAIN_GROUPS[i];
			if (unsigned(g.group) != i || !g.key || !g.label || !validTerrainProperties(g.properties))
				return false;
			// Distinct groups must stay distinct mechanics: the registry
			// deduplicates equal profiles, and the palette groups by profile.
			for (unsigned j = 0; j < i; ++j)
				if (sameTerrainProperties(g.properties, TERRAIN_GROUPS[j].properties))
					return false;
		}
		return true;
	}(),
	"Every terrain group needs a unique, valid property profile");
