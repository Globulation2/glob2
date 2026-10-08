// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrushCatalog.h"
#include "ExperimentalFeatures.h"
#include "FormatableString.h"
#include "TerrainExperiments.h"
#include "map/TerrainRegistry.h"
#include "resource/ResourceRegistry.h"
#include <algorithm>
#include <cctype>

namespace
{
struct Text
{
	const BrushCatalogInputs &inputs;
	std::optional<std::string> find(const std::string &key) const
	{
		return inputs.lookup ? inputs.lookup(key) : std::nullopt;
	}
	// String-table keys fall back to themselves, like the UI's tr().
	std::string tr(const std::string &key) const { return find(key).value_or(key); }
	// Authored names: "[name]" when the table has it, else the name itself.
	std::string authored(const std::string &name) const { return find("[" + name + "]").value_or(name); }
	std::string terrainLabel(const TerrainRegistry &registry, TerrainType type) const
	{
		const std::string label = registry.presentation(type).label;
		return unsigned(type) < TERRAIN_COUNT ? tr(label) : label;
	}
	std::string experimentLabel(const std::string &key) const
	{
		if (auto text = find("[experiment " + key + "]"))
			return *text;
		for (const auto &definition : registeredExperimentDefinitions())
			if (definition.key == key)
				return definition.label;
		if (inputs.resources)
			for (const auto &definition : inputs.resources->experiments())
				if (definition.key == key)
					return definition.label;
		return key;
	}
	std::string experimentHelp(const std::string &key) const
	{
		if (auto text = find("[experiment " + key + " help]"))
			return *text;
		if (inputs.resources)
			for (const auto &definition : inputs.resources->experiments())
				if (definition.key == key)
					return definition.help;
		for (const auto &definition : registeredExperimentDefinitions())
			if (definition.key == key)
				return definition.help;
		return {};
	}
};

bool enabled(const BrushCatalogInputs &inputs, const std::string &key)
{
	return key.empty() || (inputs.experimentEnabled && inputs.experimentEnabled(key));
}

std::string joinLines(std::initializer_list<std::string> lines)
{
	std::string text;
	for (const auto &line : lines)
		if (!line.empty())
			text += (text.empty() ? "" : "\n") + line;
	return text;
}

std::string lockedLine(const Text &text, const std::string &experiment, bool locked)
{
	if (!locked)
		return {};
	return GAGCore::FormattableString(text.tr("[brush requires experiment %0]")).arg(text.experimentLabel(experiment));
}

BrushEntry terrainEntry(const BrushCatalogInputs &inputs, const Text &text, TerrainType type,
						const std::string &group, std::string rules, const std::string &groupRules)
{
	const auto &registry = *inputs.terrain;
	BrushEntry entry;
	entry.id = "terrain/" + registry.key(type);
	entry.section = BrushSection::Terrain;
	entry.group = group;
	entry.label = text.terrainLabel(registry, type);
	entry.rules = std::move(rules);
	entry.swatch.kind = BrushSwatch::Kind::Terrain;
	entry.swatch.terrain = type;
	entry.swatch.key = registry.key(type);
	entry.action = "select terrain " + registry.key(type);
	entry.experiment = terrainExperimentKey(type).value_or("");
	entry.locked = !enabled(inputs, entry.experiment);
	entry.tooltip = joinLines({entry.label, entry.rules.empty() ? groupRules : entry.rules,
							   lockedLine(text, entry.experiment, entry.locked)});
	return entry;
}

void addTerrain(const BrushCatalogInputs &inputs, const Text &text, std::vector<BrushGroup> &catalog)
{
	const auto &registry = *inputs.terrain;
	const auto translate = [&](const std::string &key) { return text.tr(key); };
	const auto selectable = [&](TerrainType type)
	{ return registry.valid(type) && registry.presentation(type).editorSelectable; };
	auto push = [&](BrushGroup group)
	{
		if (!group.entries.empty())
			catalog.push_back(std::move(group));
	};
	{
		BrushGroup classic{"classic", BrushSection::Terrain, text.tr("[terrain group classic]"), "", {}};
		for (auto type : {WATER, SAND, GRASS})
			if (selectable(type))
				classic.entries.push_back(terrainEntry(inputs, text, type, classic.key,
													   terrainRuleSummary(registry.properties(type), translate), ""));
		push(std::move(classic));
	}
	for (unsigned g = 0; g < TERRAIN_GROUP_COUNT; ++g)
	{
		const auto group = TerrainGroup(g);
		const auto &definition = terrainGroupDefinition(group);
		if (!definition.paletteVisible || terrainGroupIsClassic(group))
			continue;
		BrushGroup entries{definition.key, BrushSection::Terrain, text.tr(definition.label),
						   terrainRuleSummary(definition.properties, translate), {}};
		for (unsigned id = 0; id < TERRAIN_COUNT; ++id)
			if (terrainGroup(TerrainType(id)) == group && selectable(TerrainType(id)))
				entries.entries.push_back(terrainEntry(inputs, text, TerrainType(id), entries.key, "", entries.rules));
		push(std::move(entries));
	}
	std::vector<TerrainType> custom;
	for (unsigned id = TERRAIN_COUNT; id < registry.size(); ++id)
		if (selectable(TerrainType(id)))
			custom.push_back(TerrainType(id));
	// Natural label order; equal labels fall back to the stable key order.
	std::stable_sort(custom.begin(), custom.end(), [&](TerrainType a, TerrainType b)
					 {
						 const std::string la = registry.presentation(a).label, lb = registry.presentation(b).label;
						 if (naturalLess(la, lb)) return true;
						 if (naturalLess(lb, la)) return false;
						 return registry.key(a) < registry.key(b);
					 });
	BrushGroup imported{"custom", BrushSection::Terrain, text.tr("[terrain group custom]"), "", {}};
	for (auto type : custom)
		imported.entries.push_back(terrainEntry(inputs, text, type, imported.key,
												terrainRuleSummary(registry.properties(type), translate), ""));
	push(std::move(imported));
}

// Where a resource may go, for tooltips: built-in terrain by group (so shore
// variants fold into their visible name), imported types by their own label.
std::string validOnText(const BrushCatalogInputs &inputs, const Text &text, const std::vector<TerrainType> &validOn)
{
	if (validOn.empty())
		return text.tr("[brush valid nowhere]");
	std::vector<std::string> names;
	for (auto type : validOn)
	{
		const auto name = unsigned(type) < TERRAIN_COUNT ? text.tr(terrainGroupDefinition(terrainGroup(type)).label)
														 : text.terrainLabel(*inputs.terrain, type);
		if (std::find(names.begin(), names.end(), name) == names.end())
			names.push_back(name);
	}
	std::string list;
	for (const auto &name : names)
		list += (list.empty() ? "" : ", ") + name;
	return GAGCore::FormattableString(text.tr("[brush valid on %0]")).arg(list);
}

void addResources(const BrushCatalogInputs &inputs, const Text &text, std::vector<BrushGroup> &catalog)
{
	const auto &registry = *inputs.resources;
	const auto &terrain = *inputs.terrain;
	std::vector<BrushGroup> groups;
	auto groupFor = [&](const std::string &key, const std::string &title, const std::string &rules) -> BrushGroup &
	{
		for (auto &group : groups)
			if (group.key == key)
				return group;
		groups.push_back({key, BrushSection::Resources, title, rules, {}});
		return groups.back();
	};
	groupFor("base", text.tr("[Resources]"), "");
	for (unsigned id = 0; id < registry.size(); ++id)
	{
		const auto resource = static_cast<ResourceId>(id);
		BrushEntry entry;
		entry.id = "resource/" + registry.key(resource);
		entry.section = BrushSection::Resources;
		entry.label = text.authored(registry.presentation(resource).name);
		entry.action = "select resource " + registry.key(resource);
		entry.experiment = registry.requiredExperiment(resource);
		entry.locked = !enabled(inputs, entry.experiment);
		entry.swatch.kind = BrushSwatch::Kind::Resource;
		entry.swatch.resource = resource;
		entry.swatch.key = registry.key(resource);
		std::optional<TerrainType> backdrop;
		for (unsigned t = 0; t < terrain.size(); ++t)
		{
			const auto type = TerrainType(t);
			if (!inputs.supportsResource || !inputs.supportsResource(type, resource))
				continue;
			entry.validOn.push_back(type);
			// Prefer a built-in, selectable backdrop so the icon sits on familiar ground.
			if (terrain.presentation(type).editorSelectable &&
				(!backdrop || (*backdrop >= TERRAIN_COUNT && t < TERRAIN_COUNT)))
				backdrop = type;
		}
		entry.swatch.terrain = backdrop.value_or(entry.validOn.empty() ? GRASS : entry.validOn.front());
		const bool custom = inputs.defaultResources && !inputs.defaultResources->find(registry.key(resource));
		BrushGroup &group = custom ? groupFor("custom", text.tr("[brush resources custom]"), "")
						   : entry.experiment.empty()
							   ? groupFor("base", "", "")
							   : groupFor(entry.experiment, text.experimentLabel(entry.experiment),
										  text.experimentHelp(entry.experiment));
		entry.group = group.key;
		entry.tooltip = joinLines({entry.label, validOnText(inputs, text, entry.validOn),
								   lockedLine(text, entry.experiment, entry.locked)});
		group.entries.push_back(std::move(entry));
	}
	for (auto &group : groups)
		if (!group.entries.empty())
			catalog.push_back(std::move(group));
}

BrushEntry simpleEntry(BrushSection section, const std::string &group, const std::string &id, std::string label,
					   std::string action, BrushSwatch::Kind kind, std::string swatchKey)
{
	BrushEntry entry;
	entry.id = id;
	entry.section = section;
	entry.group = group;
	entry.label = std::move(label);
	entry.tooltip = entry.label;
	entry.action = std::move(action);
	entry.swatch.kind = kind;
	entry.swatch.key = std::move(swatchKey);
	return entry;
}

void addObjects(const BrushCatalogInputs &inputs, const Text &text, std::vector<BrushGroup> &catalog)
{
	BrushGroup buildings{"buildings", BrushSection::Buildings, text.tr("[Buildings]"), "", {}};
	BrushGroup flags{"flags", BrushSection::Flags, text.tr("[Flags]"), "", {}};
	for (const auto &building : inputs.buildings)
	{
		auto &group = building.flag ? flags : buildings;
		group.entries.push_back(simpleEntry(group.section, group.key,
											(building.flag ? "flag/" : "building/") + building.key, building.label,
											"set place building selection " + building.key,
											BrushSwatch::Kind::Building, building.key));
	}
	for (auto *group : {&buildings, &flags})
		if (!group->entries.empty())
			catalog.push_back(std::move(*group));
	if (!inputs.includeTools)
		return;
	BrushGroup units{"units", BrushSection::Units, text.tr("[Units]"), "", {}};
	for (const auto &[key, label] : {std::pair{"worker", "[Worker]"}, {"explorer", "[Explorer]"}, {"warrior", "[Warrior]"}})
		units.entries.push_back(simpleEntry(BrushSection::Units, units.key, std::string("unit/") + key, text.tr(label),
											std::string("select ") + key, BrushSwatch::Kind::Unit, key));
	catalog.push_back(std::move(units));
	BrushGroup zones{"zones", BrushSection::Zones, text.tr("[brush section zones]"), "", {}};
	struct Zone
	{
		const char *key, *label, *action;
	};
	for (const auto &zone : {Zone{"forbidden", "[forbidden area]", "select forbidden zone"},
							 Zone{"guard", "[guard area]", "select guard zone"},
							 Zone{"clearing", "[brush clearing area]", "select clearing zone"},
							 Zone{"farm", "[farm area]", "select farm zone"}})
		zones.entries.push_back(simpleEntry(BrushSection::Zones, zones.key, std::string("zone/") + zone.key,
											text.tr(zone.label), zone.action, BrushSwatch::Kind::Zone, zone.key));
	auto &farm = zones.entries.back();
	farm.experiment = experimentDefinition(ExperimentId::FarmAreas).key;
	farm.locked = !enabled(inputs, farm.experiment);
	farm.tooltip = joinLines({farm.label, lockedLine(text, farm.experiment, farm.locked)});
	catalog.push_back(std::move(zones));
	BrushGroup areas{"areas", BrushSection::Areas, text.tr("[brush section areas]"), "", {}};
	areas.entries.push_back(simpleEntry(BrushSection::Areas, areas.key, "area/no-growth",
										text.tr("[no ressources growth areas]"), "select no ressources growth",
										BrushSwatch::Kind::Tool, "no-growth"));
	areas.entries.push_back(simpleEntry(BrushSection::Areas, areas.key, "area/script", text.tr("[Script Areas]"),
										"select change areas", BrushSwatch::Kind::Tool, "script"));
	catalog.push_back(std::move(areas));
	BrushGroup tools{"tools", BrushSection::Tools, text.tr("[brush section tools]"), "", {}};
	tools.entries.push_back(simpleEntry(BrushSection::Tools, tools.key, "tool/delete", text.tr("[delete]"),
										"select delete objects", BrushSwatch::Kind::Tool, "delete"));
	catalog.push_back(std::move(tools));
}
} // namespace

const char *brushSectionKey(BrushSection section)
{
	static constexpr const char *keys[] = {"terrain", "resources", "buildings", "flags",
										   "units",   "zones",     "areas",     "tools"};
	static_assert(std::size(keys) == std::size_t(BrushSection::Count));
	return unsigned(section) < std::size(keys) ? keys[unsigned(section)] : "";
}

std::vector<BrushGroup> buildBrushCatalog(const BrushCatalogInputs &inputs)
{
	std::vector<BrushGroup> catalog;
	if (!inputs.terrain || !inputs.resources)
		return catalog;
	const Text text{inputs};
	addTerrain(inputs, text, catalog);
	addResources(inputs, text, catalog);
	addObjects(inputs, text, catalog);
	return catalog;
}

const BrushEntry *findBrushEntry(const std::vector<BrushGroup> &catalog, std::string_view id)
{
	for (const auto &group : catalog)
		for (const auto &entry : group.entries)
			if (entry.id == id)
				return &entry;
	return nullptr;
}

const BrushGroup *findBrushGroup(const std::vector<BrushGroup> &catalog, BrushSection section, std::string_view key)
{
	for (const auto &group : catalog)
		if (group.section == section && group.key == key)
			return &group;
	return nullptr;
}

std::optional<std::string> terrainExperimentKey(TerrainType type)
{
	if (const auto id = terrainExperiment(type))
		return std::string(experimentDefinition(*id).key);
	return std::nullopt;
}

bool terrainBrushOffered(const TerrainRegistry &registry, TerrainType type, const ExperimentGate &enabled)
{
	if (!registry.valid(type) || !registry.presentation(type).editorSelectable)
		return false;
	const auto experiment = terrainExperimentKey(type);
	return !experiment || (enabled && enabled(*experiment));
}

std::vector<TerrainType> offeredTerrainBrushes(const TerrainRegistry &registry, TerrainGroup group,
											   const ExperimentGate &enabled)
{
	std::vector<TerrainType> brushes;
	for (unsigned id = 0; id < TERRAIN_COUNT; ++id)
		if (terrainGroup(TerrainType(id)) == group && terrainBrushOffered(registry, TerrainType(id), enabled))
			brushes.push_back(TerrainType(id));
	return brushes;
}

std::string terrainRuleSummary(const TerrainProperties &p, const std::function<std::string(const std::string &)> &tr)
{
	std::vector<std::string> parts;
	if (p.walkable)
		parts.push_back(tr("[terrain rule walkable]"));
	else if (p.swimmable)
		parts.push_back(tr("[terrain rule swimmable]"));
	else
		parts.push_back(tr("[terrain rule impassable]"));
	if (!p.flyable)
		parts.push_back(tr("[terrain rule no flying]"));
	if (p.buildable)
		parts.push_back(tr("[terrain rule buildable]"));
	if ((p.walkable || p.swimmable) && p.groundSpeedQ8 != 256)
		parts.push_back(GAGCore::FormattableString(tr("[terrain rule speed %0]")).arg(int(p.groundSpeedQ8) * 100 / 256));
	if (p.resourcesGrow && p.allowedResources)
		parts.push_back(tr(p.swimmable ? "[terrain rule algae grow]" : "[terrain rule crops grow]"));
	if (terrainProvidesFertility(p))
		parts.push_back(tr("[terrain rule irrigates]"));
	if (p.inhibitionQ8)
		parts.push_back(tr("[terrain rule stops nearby growth]"));
	if (p.projectileBlocks)
		parts.push_back(tr("[terrain rule blocks shots]"));
	if (p.groundHealthQ8 < 0)
		parts.push_back(tr("[terrain rule hurts walkers]"));
	if (p.airHealthQ8 < 0)
		parts.push_back(tr("[terrain rule hurts fliers]"));
	std::string text;
	for (const auto &part : parts)
		text += (text.empty() ? "" : "  ·  ") + part;
	return text;
}

bool naturalLess(const std::string &a, const std::string &b)
{
	std::size_t i = 0, j = 0;
	while (i < a.size() && j < b.size())
	{
		if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j])))
		{
			std::size_t ei = i, ej = j;
			while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
			while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
			const auto da = a.substr(i, ei - i), db = b.substr(j, ej - j);
			const auto ta = da.substr(std::min(da.find_first_not_of('0'), da.size() - 1));
			const auto tb = db.substr(std::min(db.find_first_not_of('0'), db.size() - 1));
			if (ta.size() != tb.size()) return ta.size() < tb.size();
			if (ta != tb) return ta < tb;
			i = ei; j = ej;
			continue;
		}
		if (a[i] != b[j]) return a[i] < b[j];
		++i; ++j;
	}
	return a.size() - i < b.size() - j;
}
