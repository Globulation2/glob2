// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "ExperimentalFeatures.h"

#include "FileFormatVersions.h"
#include <Stream.h>
#include <StringTable.h>
#include <Toolkit.h>

#include <iostream>
#include <sstream>

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

std::vector<std::string> ExperimentSet::keys() const
{
	std::vector<std::string> result;
	for (const auto &definition : experimentDefinitions())
		if (has(definition.id))
			result.push_back(definition.key);
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

ExperimentSet ExperimentSet::fromKeys(const std::vector<std::string> &keys, std::vector<std::string> *unknown)
{
	ExperimentSet set;
	for (const auto &key : keys)
	{
		if (key.empty())
			continue;
		if (const auto id = parseExperimentKey(key))
			set.set(*id);
		else if (unknown)
			unknown->push_back(key);
	}
	return set;
}

ExperimentSet ExperimentSet::fromText(const std::string &text, std::vector<std::string> *unknown)
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
	return fromKeys(keys, unknown);
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

bool ExperimentSet::load(GAGCore::InputStream *stream, Sint32 versionMinor, bool rejectUnknown)
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
		if (const auto id = parseExperimentKey(key))
			set(*id);
		else if (rejectUnknown)
			return false;
		else
			std::cerr << "ExperimentSet::load: ignoring unknown experiment \"" << key << "\"" << std::endl;
	}
	stream->readLeaveSection();
	return true;
}

std::string experimentLabelList(const ExperimentSet &set)
{
	std::string text;
	for (const auto &key : set.keys())
	{
		if (!text.empty())
			text += ", ";
		text += GAGCore::Toolkit::getStringTable()->getString("[experiment " + key + "]");
	}
	return text;
}
