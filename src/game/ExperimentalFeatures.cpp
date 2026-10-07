// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "ExperimentalFeatures.h"

#include "FileFormatVersions.h"
#include <Stream.h>
#include <StringTable.h>
#include <Toolkit.h>

#include <iostream>
#include <algorithm>
#include <sstream>
#include <stdexcept>

const std::vector<ExperimentDefinition> &experimentDefinitions()
{
	static const std::vector<ExperimentDefinition> definitions = {
		{ExperimentId::GuardAreaBalancing, "guard-area-balancing", "Guard-area balancing",
		 "Free warriors spread between painted guard areas instead of all taking the nearest one: a bigger area gets more of them, and an over-full area thins out into the others."},
		{ExperimentId::FarmAreas, "farm-areas", "Farm areas",
		 "Adds a farm area to the zone brushes. Workers harvesting inside one take from the ripest tile of the connected field and leave one grain on every tile, so the field regrows instead of being eaten from the edge. Wood growing into a farm is cleared."},
		{ExperimentId::IceTerrain, "ice-terrain", "Ice terrain",
		 "Adds ice to the map editor. Ground units move at half speed and lose one health point per 32 exposed ticks. Ice cannot hold buildings or resources."},
		{ExperimentId::TrailTerrain, "road-terrain", "Trail terrain",
		 "Adds weathered trails to the map editor. Ground units move at double speed. Trails support buildings but no resources. Flying units are unaffected."},
		{ExperimentId::MarketsV2, "markets-v2", "Markets V2",
		 "Workers can fetch supplies from shared market stock. Markets can upgrade to store food and wood, then every material. Trading between teams remains fruit-only. Applies to new games; saves keep their original setting."},
		{ExperimentId::ObstacleTerrain, "obstacle-terrain", "Obstacle terrain",
		 "Adds boulders, hedges and thickets to the map editor. Ground units cannot cross them and they stop projectiles; flying units pass over. They hold neither buildings nor resources."},
		{ExperimentId::RidgeTerrain, "ridge-terrain", "Ridge terrain",
		 "Adds rock ridges and outcrops to the map editor. Ground units cannot cross them, but towers shoot over them and flying units pass. They hold neither buildings nor resources."},
		{ExperimentId::BarrenTerrain, "barren-terrain", "Barren ground",
		 "Adds dirt, clay, gravel and flower meadows to the map editor. Buildings can stand on them, nothing grows on or next to them, and they do not count as shoreline."},
		{ExperimentId::RoughTerrain, "rough-terrain", "Rough ground",
		 "Adds mud, marsh, deep snow and scree to the map editor. Ground units move at about two thirds speed. Rough ground holds neither buildings nor resources."},
		{ExperimentId::PathTerrain, "path-terrain", "Path terrain",
		 "Adds dirt tracks and boardwalks to the map editor. Like trails, ground units move at double speed and buildings are allowed, but nothing grows."},
		{ExperimentId::LavaTerrain, "lava-terrain", "Lava terrain",
		 "Adds lava and ember fields to the map editor. Ground units cannot enter them; flying units crossing them lose health quickly. Projectiles pass over."},
		{ExperimentId::FertileTerrain, "fertile-terrain", "Fertile ground",
		 "Adds loam, moss and spring meadows to the map editor. They support buildings and crops and irrigate the land around them more strongly than water does."},
		{ExperimentId::DeepWaterTerrain, "deep-water-terrain", "Deep water",
		 "Adds deep and dark water to the map editor. Swimmers cross them more slowly, no algae grows in them, and they still irrigate nearby fields."},
		{ExperimentId::VoidTerrain, "void-terrain", "Void terrain",
		 "Adds holes and chasms to the map editor. Nothing can cross them, not even flying units, and projectiles stop at their edge."},
	};
	return definitions;
}

const ExperimentDefinition &experimentDefinition(ExperimentId id)
{
	return experimentDefinitions().at(static_cast<std::size_t>(id));
}

std::optional<ExperimentId> parseExperimentKey(const std::string &key)
{
	for (const auto &definition : experimentDefinitions())
		if (key == definition.key)
			return definition.id;
	return std::nullopt;
}

namespace
{
CatalogExperimentRegistry &catalogRegistry()
{
	static CatalogExperimentRegistry registry;
	return registry;
}

bool validKey(const std::string &key)
{
	return !key.empty() && key.size() <= 128 && key.front() != '-' && key.back() != '-' &&
		key.find("--") == std::string::npos &&
		std::all_of(key.begin(), key.end(), [](unsigned char c) {
			return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
		});
}

std::string translated(const CatalogExperimentDefinition &definition, bool help)
{
	const std::string key = "[experiment " + definition.key + (help ? " help]" : "]");
	const auto *strings = GAGCore::Toolkit::getStringTable();
	if (strings && strings->doesStringExist(key))
		return strings->getString(key);
	return help ? definition.help : definition.label;
}
}

void validateCatalogExperiments(const std::vector<CatalogExperimentDefinition> &definitions)
{
	std::set<std::string> seen;
	std::size_t customCount=0;
	for (const auto &definition : definitions)
	{
		if (!validKey(definition.key) || definition.label.empty() || definition.help.empty())
			throw std::invalid_argument("Invalid catalog experiment definition: " + definition.key);
		if (!seen.insert(definition.key).second)
			throw std::invalid_argument("Duplicate catalog experiment: " + definition.key);
		if (!parseExperimentKey(definition.key)) ++customCount;
	}
	if (customCount + ExperimentSet::COUNT > ExperimentSet::MAX_STORED)
		throw std::invalid_argument("Too many catalog experiments");
}

void CatalogExperimentRegistry::install(const std::vector<CatalogExperimentDefinition> &definitions)
{
	validateCatalogExperiments(definitions);
	std::vector<CatalogExperimentDefinition> candidate;
	for (const auto &definition : definitions)
		// Built-in definitions keep their established labels and enum identity.
		if (!parseExperimentKey(definition.key)) candidate.push_back(definition);
	std::sort(candidate.begin(), candidate.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
	if (installed && candidate != entries)
		throw std::logic_error("Catalog experiments cannot change after startup");
	entries = std::move(candidate);
	installed = true;
}

void registerCatalogExperiments(const std::vector<CatalogExperimentDefinition> &definitions)
{
	catalogRegistry().install(definitions);
}

void saveCatalogExperimentDefinitions(GAGCore::OutputStream* stream, const std::vector<CatalogExperimentDefinition>& definitions)
{
	validateCatalogExperiments(definitions);
	stream->writeEnterSection("resourceExperimentDefinitions");
	stream->writeUint32(definitions.size(), "count");
	for (unsigned i = 0; i < definitions.size(); ++i)
	{
		const auto& definition = definitions[i];
		if (definition.key.size() > 128 || definition.label.size() > 512 || definition.help.size() > 4096)
			throw std::invalid_argument("Resource experiment metadata exceeds limits");
		stream->writeEnterSection(i);
		stream->writeText(definition.key, "key");
		stream->writeText(definition.label, "label");
		stream->writeText(definition.help, "help");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}

std::vector<CatalogExperimentDefinition> loadCatalogExperimentDefinitions(GAGCore::InputStream* stream)
{
	stream->readEnterSection("resourceExperimentDefinitions");
	const auto count = stream->readUint32("count");
	if (count > ExperimentSet::MAX_STORED) throw std::invalid_argument("Too many resource experiment definitions");
	std::vector<CatalogExperimentDefinition> result;
	for (unsigned i = 0; i < count; ++i)
	{
		stream->readEnterSection(i);
		CatalogExperimentDefinition definition{stream->readText("key"), stream->readText("label"), stream->readText("help")};
		if (definition.key.size() > 128 || definition.label.size() > 512 || definition.help.size() > 4096)
			throw std::invalid_argument("Resource experiment metadata exceeds limits");
		result.push_back(std::move(definition));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	validateCatalogExperiments(result);
	return result;
}

std::vector<CatalogExperimentDefinition> registeredExperimentDefinitions()
{
	std::vector<CatalogExperimentDefinition> definitions;
	for (const auto &builtin : experimentDefinitions())
		definitions.push_back({builtin.key, builtin.label, builtin.help});
	const auto &catalog = catalogRegistry().definitions();
	definitions.insert(definitions.end(), catalog.begin(), catalog.end());
	return definitions;
}

bool knownExperimentKey(const std::string &key, const std::vector<std::string> &allowedKeys)
{
	if (parseExperimentKey(key)) return true;
	if (!validKey(key)) return false;
	const auto &catalog = catalogRegistry().definitions();
	return std::any_of(catalog.begin(), catalog.end(), [&](const auto &definition) { return definition.key == key; }) ||
		std::find(allowedKeys.begin(), allowedKeys.end(), key) != allowedKeys.end();
}

std::string experimentLabel(const CatalogExperimentDefinition &definition) { return translated(definition, false); }
std::string experimentHelp(const CatalogExperimentDefinition &definition) { return translated(definition, true); }

void ExperimentSet::set(ExperimentId id, bool on)
{
	if (on && !has(id) && size() >= MAX_STORED)
		throw std::length_error("Too many enabled experiments");
	bits.set(static_cast<std::size_t>(id), on);
}

bool ExperimentSet::has(const std::string &key) const
{
	if (const auto id = parseExperimentKey(key)) return has(*id);
	return dynamicKeys.contains(key);
}

void ExperimentSet::set(const std::string &key, bool on, const std::vector<std::string> &allowedKeys)
{
	if (const auto id = parseExperimentKey(key))
	{
		set(*id, on);
		return;
	}
	if (!dynamicKeys.contains(key) && !knownExperimentKey(key, allowedKeys))
		throw std::invalid_argument("Unknown experiment: " + key);
	if (on)
	{
		if (!dynamicKeys.contains(key) && size() >= MAX_STORED)
			throw std::length_error("Too many enabled experiments");
		dynamicKeys.insert(key);
	}
	else dynamicKeys.erase(key);
}

std::vector<std::string> ExperimentSet::keys() const
{
	std::vector<std::string> result;
	for (const auto &definition : experimentDefinitions())
		if (has(definition.id))
			result.push_back(definition.key);
	result.insert(result.end(), dynamicKeys.begin(), dynamicKeys.end());
	return result;
}

std::string ExperimentSet::toText() const
{
	std::string text;
	for (const auto &key : keys())
	{
		if (!text.empty())
			text += ',';
		text += key;
	}
	return text;
}

ExperimentSet ExperimentSet::fromKeys(const std::vector<std::string> &keys, std::vector<std::string> *unknown,
	const std::vector<std::string> &allowedKeys)
{
	ExperimentSet set;
	for (const auto &key : keys)
	{
		if (key.empty())
			continue;
		if (knownExperimentKey(key, allowedKeys))
			set.set(key, true, allowedKeys);
		else if (unknown)
			unknown->push_back(key);
	}
	return set;
}

ExperimentSet ExperimentSet::fromText(const std::string &text, std::vector<std::string> *unknown,
	const std::vector<std::string> &allowedKeys)
{
	std::vector<std::string> keys;
	std::stringstream list(text);
	std::string item;
	while (std::getline(list, item, ','))
	{
		const auto begin = item.find_first_not_of(" \t\r\n");
		const auto end = item.find_last_not_of(" \t\r\n");
		if (begin != std::string::npos)
			keys.push_back(item.substr(begin, end - begin + 1));
	}
	return fromKeys(keys, unknown, allowedKeys);
}

void ExperimentSet::save(GAGCore::OutputStream *stream, const char *section) const
{
	const auto enabled = keys();
	stream->writeEnterSection(section);
	stream->writeUint32(static_cast<Uint32>(enabled.size()), "count");
	for (std::size_t i = 0; i < enabled.size(); ++i)
	{
		stream->writeEnterSection(static_cast<unsigned>(i));
		stream->writeText(enabled[i], "key");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}

bool ExperimentSet::load(GAGCore::InputStream *stream, Sint32 versionMinor, bool rejectUnknown,
	const std::vector<std::string> &allowedKeys, const char *section)
{
	clear();
	if (versionMinor < FILE_FORMAT_VERSION_EXPERIMENTS)
		return true;
	stream->readEnterSection(section);
	const Uint32 count = stream->readUint32("count");
	if (count > MAX_STORED)
	{
		stream->readLeaveSection();
		return false;
	}
	for (Uint32 i = 0; i < count; ++i)
	{
		stream->readEnterSection(i);
		const std::string key = stream->readText("key");
		stream->readLeaveSection();
		if (key.empty() || key.size() > 128)
		{
			stream->readLeaveSection();
			return false;
		}
		if (knownExperimentKey(key, allowedKeys))
			set(key, true, allowedKeys);
		else if (rejectUnknown)
		{
			stream->readLeaveSection();
			return false;
		}
		else
			std::cerr << "ExperimentSet::load: ignoring unknown experiment \"" << key << "\"" << std::endl;
	}
	stream->readLeaveSection();
	return true;
}

std::string experimentLabelList(const ExperimentSet &set)
{
	std::string text;
	const auto definitions = registeredExperimentDefinitions();
	for (const auto &key : set.keys())
	{
		if (!text.empty())
			text += ", ";
		const auto definition = std::find_if(definitions.begin(), definitions.end(),
			[&](const auto &item) { return item.key == key; });
		// A removed local definition can still belong to an embedded catalog.
		text += definition == definitions.end() ? key : experimentLabel(*definition);
	}
	return text;
}
