// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainGroup.h"
#include "TerrainPresentation.h"
#include "TerrainType.h"
#include <optional>
#include <string_view>

// One row per built-in TerrainType, defined in TerrainCatalog.cpp: its group (from
// TerrainGroupTable.h), external name, string-table label and semantic colours.
// TerrainPresentations derives from these rows.
struct TerrainTypeDefinition
{
	TerrainType id;
	TerrainGroup group;
	// External name for scripts, files and material bindings; label is a string-table key.
	const char *name, *label;
	TerrainColor minimap, overview, image, preview;
};

const TerrainTypeDefinition &terrainTypeDefinition(TerrainType type);

// The built-in that a current or legacy external name denotes. Earlier names of
// renamed built-ins still resolve, because saved custom registries, map sets,
// scripts, editor actions and authoring JSON may use them; files and reports
// always write the row name. Callers apply their own paintable or registry checks.
std::optional<TerrainType> terrainTypeByName(std::string_view name);
