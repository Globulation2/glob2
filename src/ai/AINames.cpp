// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <algorithm>

#include "AI.h"
#include "AINames.h"
#include "Toolkit.h"
#include "StringTable.h"

using namespace GAGCore;

namespace AINames
{
	namespace
	{
		// Single source of truth for the AI roster, in display order — one
		// row per AI::ImplementitionID. Everything user-facing about an AI's
		// identity is derived from here, so the CLI parsers, their error/help
		// text, and the localized UI labels can never drift apart.
		//   cliName   — lowercase name accepted by --ai-types/--matchup, or
		//               nullptr when the AI can't be picked from the CLI (NONE).
		//   stringKey — StringTable base key: the display name is "[<key>]"
		//               and the description "[<key>-Description]".
		const struct { int id; const char* cliName; const char* stringKey; } aiTable[] = {
			{AI::NONE,            nullptr,           "AINone"},
			{AI::NUMBI,           "numbi",           "AINumbi"},
			{AI::CASTOR,          "castor",          "AICastor"},
			{AI::WARRUSH,         "warrush",         "AIWarrush"},
			{AI::REACHTOINFINITY, "reachtoinfinity", "AIReachToInfinity"},
			{AI::NICOWAR,         "nicowar",         "AINicowar"},
			{AI::CORTEX,          "cortex",          "AICortex"},
		};
	}

	std::string getAIText(int id)
	{
		for (const auto& entry : aiTable)
			if (entry.id == id)
				return Toolkit::getStringTable()->getString("[" + std::string(entry.stringKey) + "]");
		return "unknown AI";
	}

	std::string getAIDescription(int id)
	{
		for (const auto& entry : aiTable)
			if (entry.id == id)
				return Toolkit::getStringTable()->getString("[" + std::string(entry.stringKey) + "-Description]");
		return "unknown AI";
	}

	int parseAIName(const std::string& name)
	{
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
		for (const auto& entry : aiTable)
		{
			if (entry.cliName && lower == entry.cliName)
				return entry.id;
		}
		return AI_UNKNOWN_NAME;
	}

	std::string validAINames()
	{
		std::string list;
		for (const auto& entry : aiTable)
		{
			if (!entry.cliName)
				continue;
			if (!list.empty())
				list += ", ";
			list += entry.cliName;
		}
		return list;
	}
}
