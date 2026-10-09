// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's brush catalogue, experiment gate and active-brush identity
// (BrushCatalog.h). Presentations read these instead of the side-panel widgets.
#include "BuildingPresentation.h"
#include "ExperimentalFeatures.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "resource/ResourceRegistry.h"

bool MapEdit::experimentEnabled(const std::string& key) const
{
	if (key.empty())
		return true;
	return (globalContainer && globalContainer->settings.experiments.has(key)) ||
		(view.scene && view.scene->world.rules->configuration->getExperiments().has(key));
}

ExperimentGate MapEdit::experimentGate() const
{
	return [this](const std::string& key) { return experimentEnabled(key); };
}

bool MapEdit::enableExperimentForMap(const std::string& key)
{
	auto& experiments = game.gameHeader.getExperiments();
	if (experiments.has(key))
		return true;
	auto allowed = game.gameHeader.catalogExperimentKeys();
	for (const auto& resourceKey : game.map.resourceRegistry().experimentKeys())
		allowed.push_back(resourceKey);
	try
	{
		experiments.set(key, true, allowed);
	}
	catch (const std::exception&)
	{
		return false;
	}
	mapHasBeenModified();
    if (view.scene) preparePresentation();
	++brushCatalogRevision;
	return true;
}

BrushCatalogInputs MapEdit::brushCatalogInputs() const
{
	BrushCatalogInputs inputs;
	inputs.terrain = &view.scene->map.terrainRegistry();
	inputs.resources = &view.scene->map.resourceRegistry();
	inputs.defaultResources = ResourceRegistry::availableDefaults().get();
	inputs.experimentEnabled = experimentGate();
	inputs.supportsResource = [this](TerrainType terrain, ResourceId resource)
	{ return MapState::terrainSupportsResourceType(view.scene->world.view(), terrain, resource); };
	inputs.lookup = [](const std::string& key) -> std::optional<std::string>
	{
		const auto* strings = GAGCore::Toolkit::getStringTable();
		if (!strings || !strings->doesStringExist(key))
			return std::nullopt;
		return std::string(strings->getString(key));
	};
	for (std::size_t id = 0; id < view.scene->buildingTypes->size(); ++id)
	{
		const auto* type = &(*view.scene->buildingTypes)[id];
		if (!type->runtimeAvailable || !type->semantics.placeable)
			continue;
		inputs.buildings.push_back({type->key, buildingDisplayName(*type), !type->semantics.occupiesGround});
	}
	return inputs;
}

std::string MapEdit::brushCatalogSignature() const
{
	std::string signature = view.scene->map.terrainRegistry().digest();
	signature += '|' + view.scene->map.resourceRegistry().digest();
	signature += '|' + (globalContainer ? globalContainer->settings.experiments.toText() : std::string());
	signature += '|' + view.scene->world.rules->configuration->getExperiments().toText();
	signature += '|';
	for (std::size_t id = 0; id < view.scene->buildingTypes->size(); ++id)
	{
		const auto* type = &(*view.scene->buildingTypes)[id];
		if (type->runtimeAvailable && type->semantics.placeable)
			signature += type->key + ',';
	}
	return signature;
}

const std::vector<BrushGroup>& MapEdit::brushCatalog()
{
    if (!view.scene) return brushCatalogCache;
	auto signature = brushCatalogSignature();
	if (signature != brushCatalogKey || brushCatalogCache.empty())
	{
		brushCatalogCache = buildBrushCatalog(brushCatalogInputs());
		brushCatalogKey = std::move(signature);
		++brushCatalogRevision;
	}
	return brushCatalogCache;
}

std::uint64_t MapEdit::catalogRevision()
{
	brushCatalog();
	return brushCatalogRevision;
}

const BrushEntry* MapEdit::findBrush(std::string_view id)
{
	return findBrushEntry(brushCatalog(), id);
}

BrushSwatches& MapEdit::brushSwatches()
{
	if (!swatches)
		swatches = std::make_unique<BrushSwatches>();
	if (view.scene) swatches->bind(view.scene->world.terrain->registry, view.scene->world.catalogs->resources, view.scene->world.catalogs->assets);
	return *swatches;
}

TerrainSelector::TerrainType MapEdit::canonicalSelector(TerrainSelector::TerrainType type) const
{
	if (TerrainSelector::isBaseTerrain(type))
		return TerrainSelector::selectorFor(TerrainSelector::baseTerrain(type));
	if (view.scene && TerrainSelector::isResource(type))
	{
		const auto resource = TerrainSelector::resourceType(type, view.scene->map.resourceRegistry());
		if (view.scene->map.resourceRegistry().valid(resource))
			return TerrainSelector::selectorForResource(resource);
	}
	return type;
}

std::string MapEdit::currentBrushId() const
{
    if (!view.scene) return {};
	switch (selectionMode)
	{
	case PlaceTerrain:
	{
		const auto type = canonicalSelector(terrainType);
		if (TerrainSelector::isBaseTerrain(type))
		{
			const auto terrain = TerrainSelector::baseTerrain(type);
			return view.scene->map.terrainRegistry().valid(terrain) ? "terrain/" + view.scene->map.terrainRegistry().key(terrain) : "";
		}
		if (TerrainSelector::isResource(type))
		{
			const auto resource = TerrainSelector::resourceType(type, view.scene->map.resourceRegistry());
			return view.scene->map.resourceRegistry().valid(resource) ? "resource/" + view.scene->map.resourceRegistry().key(resource) : "";
		}
		return "";
	}
	case PlaceBuilding:
	{
		for (std::size_t id = 0; id < view.scene->buildingTypes->size(); ++id)
		{
			const auto* type = &(*view.scene->buildingTypes)[id];
			if (type->key == selectionName)
				return (type->semantics.occupiesGround ? "building/" : "flag/") + selectionName;
		}
		return "";
	}
	case PlaceUnit:
		switch (placingUnit)
		{
		case Worker: return "unit/worker";
		case Explorer: return "unit/explorer";
		case Warrior: return "unit/warrior";
		default: return "";
		}
	case PlaceZone:
		switch (brushType)
		{
		case ForbiddenBrush: return "zone/forbidden";
		case GuardAreaBrush: return "zone/guard";
		case ClearAreaBrush: return "zone/clearing";
		case FarmAreaBrush: return "zone/farm";
		default: return "";
		}
	case RemoveObject:
		return "tool/delete";
	case ChangeAreas:
		return "area/script";
	case ChangeNoResourceGrowthAreas:
		return "area/no-growth";
	default:
		return "";
	}
}
