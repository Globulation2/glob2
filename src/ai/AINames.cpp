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
		// row per AI::ImplementationID. Everything user-facing about an AI's
		// identity is derived from here, so the CLI parsers, their error/help
		// text, and the localized UI labels can never drift apart.
		//   cliName   — lowercase name accepted by --ai-types/--matchup, or
		//               nullptr when the AI can't be picked from the CLI (NONE).
		//   stringKey — StringTable base key: the display name is "[<key>]"
		//               and the description "[<key>-Description]".
		const struct { int id; const char* cliName; const char* stringKey; const char* difficulty; } aiTable[] = {
			{AI::NONE,            nullptr,           "AINone", "No AI orders"},
			{AI::NUMBI,           "numbi",           "AINumbi", "Easy"},
			{AI::CASTOR,          "castor",          "AICastor", "Medium"},
			{AI::WARRUSH,         "warrush",         "AIWarrush", "Medium"},
			{AI::ECONO, "econo", "AIEcono", "Easy"},
			{AI::NICOWAR,         "nicowar",         "AINicowar", "Hard"},
			{AI::CORTEX,          "cortex",          "AICortex", "Medium"},
			{AI::CABINO,          "cabino",          "AICabino", "Hard"},
		};
	}

	const std::vector<int>& selectionOrder()
	{
		static const std::vector<int> order = {AI::ECONO, AI::NUMBI, AI::WARRUSH, AI::CASTOR, AI::CORTEX, AI::NICOWAR, AI::CABINO, AI::NONE};
		return order;
	}
	int selectionIndex(int id)
	{
		const auto& order = selectionOrder();
		return int(std::find(order.begin(), order.end(), id) - order.begin());
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

    std::string getAISelectorText(int id)
    {
        for (const auto& entry : aiTable)
            if (entry.id == id)
                return (id == AI::NONE ? Toolkit::getStringTable()->getString("[Inactive]") : getAIText(id))
                    + std::string(" - ") + Toolkit::getStringTable()->getString("[" + std::string(entry.difficulty) + "]")
                    + (id == AI::ECONO ? std::string(" - ") + Toolkit::getStringTable()->getString("[No warriors]") : "");
        return "unknown AI";
    }
    std::string getAISummary(int id)
    {
        for (const auto& entry : aiTable)
            if (entry.id == id)
                return Toolkit::getStringTable()->getString("[" + std::string(entry.stringKey) + "-Summary]");
        return "unknown AI";
    }
    std::string getAIProfile(int id)
    {
        for (const auto& entry : aiTable)
            if (entry.id == id)
                {
                    std::string profile=Toolkit::getStringTable()->getString("[" + std::string(entry.stringKey) + "-Profile]");
                    for(size_t p=0;(p=profile.find("\\n",p))!=std::string::npos;++p)profile.replace(p,2,"\n");
                    return getAISelectorText(id)+"\n\n"+getAISummary(id)+"\n\n"+profile;
                }
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
