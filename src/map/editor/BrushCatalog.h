// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The editor's brush catalogue: one presentation-free list of everything an author
// can paint or place, grouped the way every editor presentation (desktop dock,
// phone tray, terrain palette) shows it. The model is pure: it reads immutable
// registries and injected callbacks only, so it never draws, never touches
// globalContainer and can be built in unit tests. MapEdit caches one catalogue per
// map state (MapEdit::brushCatalog) and swatches come from BrushSwatches.
//
// Every entry carries a stable id and the editor action that selects it:
//   terrain/<registry key>   select terrain <key>
//   resource/<registry key>  select resource <key>
//   building/<type key>      set place building selection <key>
//   flag/<type key>          set place building selection <key>
//   unit/<worker|explorer|warrior>, zone/<forbidden|guard|clearing|farm>,
//   area/<no-growth|script>, tool/delete
// performAction(entry.action) followed by MapEdit::currentBrushId() yields
// entry.id for every unlocked entry, so presentations highlight uniformly.

#include "map/TerrainGroup.h"
#include "map/TerrainType.h"
#include "resource/ResourceProperties.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class TerrainRegistry;
class ResourceRegistry;
struct TerrainProperties;

enum class BrushSection : std::uint8_t
{
	Terrain,
	Resources,
	Buildings,
	Flags,
	Units,
	Zones,
	Areas,
	Tools,
	Count
};
// Stable lower-case key ("terrain", "resources", ...) for widget keys and tests.
const char *brushSectionKey(BrushSection section);

// What a presentation draws for an entry. BrushSwatches composes Terrain and
// Resource swatches; the other kinds name the object in `key` and are drawn by
// the presentation itself (BrushSwatches returns nullptr for them).
struct BrushSwatch
{
	enum class Kind : std::uint8_t
	{
		None,
		Terrain,
		Resource,
		Building,
		Unit,
		Zone,
		Tool
	};
	Kind kind = Kind::None;
	// Terrain: the painted type. Resource: the terrain drawn behind the icon, the
	// first editor-selectable terrain the resource may be placed on.
	TerrainType terrain = GRASS;
	ResourceId resource = NoResource;
	// Building/flag type key, unit/zone/tool name.
	std::string key;
};

struct BrushEntry
{
	// Stable catalogue id, see the table at the top of this file.
	std::string id;
	BrushSection section = BrushSection::Terrain;
	// Key of the BrushGroup holding this entry.
	std::string group;
	// Translated, ready to display.
	std::string label;
	// This entry's own rule line when its group has no shared rules (classic and
	// custom terrain); empty otherwise.
	std::string rules;
	// Label, rules, placement validity and any experiment requirement, one per line.
	std::string tooltip;
	BrushSwatch swatch;
	// MapEdit::performAction string that selects this brush.
	std::string action;
	// Shown but not selectable until `experiment` is enabled for the map
	// (MapEdit::enableExperimentForMap).
	bool locked = false;
	// Experiment key gating the entry, empty when none.
	std::string experiment;
	// Resources: every terrain of the map's registry that accepts the resource,
	// in registry order. Empty for other sections.
	std::vector<TerrainType> validOn;
};

struct BrushGroup
{
	// Unique within its section: "classic", a terrain catalogue group key such as
	// "obstacles", "custom"; "base", an experiment key or "custom" for resources;
	// "buildings", "flags", "units", "zones", "areas", "tools" otherwise.
	std::string key;
	BrushSection section = BrushSection::Terrain;
	// Translated heading and the rules all members share (may be empty).
	std::string title, rules;
	std::vector<BrushEntry> entries;
};

// Whether an experiment key is enabled for the edited map (settings or map header).
using ExperimentGate = std::function<bool(const std::string &key)>;

struct BrushCatalogInputs
{
	struct Building
	{
		std::string key;
		// Translated display name.
		std::string label;
		// Overlay types (flags) go to the Flags section.
		bool flag = false;
	};
	// Required.
	const TerrainRegistry *terrain = nullptr;
	const ResourceRegistry *resources = nullptr;
	// Optional: resources whose key is absent here are listed as imported ("custom").
	const ResourceRegistry *defaultResources = nullptr;
	// Unset means every experiment is disabled.
	ExperimentGate experimentEnabled;
	// Placement validity, normally Map::terrainSupportsResourceType. Unset means
	// valid nowhere.
	std::function<bool(TerrainType, ResourceId)> supportsResource;
	// String-table lookup returning nullopt for missing keys. Unset means keys are
	// shown as-is, which keeps tests independent of the string table.
	std::function<std::optional<std::string>(const std::string &key)> lookup;
	// Placeable building types in display order (Buildings and Flags sections).
	std::vector<Building> buildings;
	// Adds the Units, Zones, Areas and Tools sections.
	bool includeTools = true;
};

// Sections in BrushSection order. Terrain: the classic water, sand and grass,
// then every palette-visible catalogue group with all its selectable members,
// then the map's imported types in natural label order. Resources: every
// registry entry. Groups without entries are omitted.
std::vector<BrushGroup> buildBrushCatalog(const BrushCatalogInputs &inputs);
// Linear lookup by entry id; nullptr when absent.
const BrushEntry *findBrushEntry(const std::vector<BrushGroup> &catalog, std::string_view id);
const BrushGroup *findBrushGroup(const std::vector<BrushGroup> &catalog, BrushSection section, std::string_view key);

// Classic ground (water, sand, grass) has its own section of the catalogue.
constexpr bool terrainGroupIsClassic(TerrainGroup group)
{
	return group == TerrainGroup::Water || group == TerrainGroup::Sand || group == TerrainGroup::Grass;
}
// The experiment key gating a terrain type, if any (built-in catalogue types only).
std::optional<std::string> terrainExperimentKey(TerrainType type);
// Whether the editor offers a type: selectable, and its experiment (if any) enabled.
bool terrainBrushOffered(const TerrainRegistry &registry, TerrainType type, const ExperimentGate &enabled);
// The offered built-in members of a catalogue group, in table order.
std::vector<TerrainType> offeredTerrainBrushes(const TerrainRegistry &registry, TerrainGroup group,
											   const ExperimentGate &enabled);
// One line of plain rules read from the properties, so a brush tells the author
// what it does without a per-type string. `translate` maps string-table keys.
std::string terrainRuleSummary(const TerrainProperties &properties,
							   const std::function<std::string(const std::string &)> &translate);
// "Terrain 2" sorts before "Terrain 10": digit runs compare by value.
bool naturalLess(const std::string &a, const std::string &b);
