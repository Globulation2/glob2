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
		 "Workers can fetch supplies from shared market stock. Markets can upgrade to store wheat and wood, then every resource. Trading between teams remains fruit-only. Applies to new games; saves keep their original setting."},
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

void CatalogExperimentRegistry::install(const std::vector<CatalogExperimentDefinition> &definitions)
{
	std::vector<CatalogExperimentDefinition> candidate;
	std::set<std::string> seen;
	for (const auto &definition : definitions)
	{
		if (!validKey(definition.key) || definition.label.empty() || definition.help.empty())
			throw std::invalid_argument("Invalid catalog experiment definition: " + definition.key);
		if (!seen.insert(definition.key).second)
			throw std::invalid_argument("Duplicate catalog experiment: " + definition.key);
		// Built-in definitions keep their established labels and enum identity.
		if (!parseExperimentKey(definition.key))
			candidate.push_back(definition);
	}
	if (candidate.size() + ExperimentSet::COUNT > ExperimentSet::MAX_STORED)
		throw std::invalid_argument("Too many catalog experiments");
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

void ExperimentSet::save(GAGCore::OutputStream *stream) const
{
	const auto enabled = keys();
	stream->writeEnterSection("experiments");
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
	const std::vector<std::string> &allowedKeys)
{
	clear();
	if (versionMinor < FILE_FORMAT_VERSION_EXPERIMENTS)
		return true;
	stream->readEnterSection("experiments");
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
