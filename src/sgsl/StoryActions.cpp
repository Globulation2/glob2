// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat
// Copyright (C) 2001-2008 Luc-Olivier de Charrière
// Copyright (C) 2001-2008 Martin S. Nyffenegger

/*!	\file StoryActions.cpp
	\brief SGSL generic functions: the script-callable Story actions and their argument tables
*/

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>

#include "GameGUI.h"
#include "sim/ClientCommandSink.h"
#include "Player.h"
#include "SGSL.h"

void Story::toto(StoryContext* gui)
{
	std::cout << "toto func : ";
	std::cout << SGSLToken::getNameByType(line[++lineSelector].type) << " ";
	std::cout << line[++lineSelector].value << "\n";
}

void Story::objectiveHidden(StoryContext* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game->objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game->objectives.getScriptNumber(i) == n)
		{
			gui->game->objectives.setObjectiveHidden(i);
			break;
		}
	}
}

void Story::objectiveVisible(StoryContext* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game->objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game->objectives.getScriptNumber(i) == n)
		{
			gui->game->objectives.setObjectiveVisible(i);
			break;
		}
	}
}

void Story::objectiveComplete(StoryContext* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game->objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game->objectives.getScriptNumber(i) == n)
		{
			gui->game->objectives.setObjectiveComplete(i);
			break;
		}
	}
}

void Story::objectiveFailed(StoryContext* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game->objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game->objectives.getScriptNumber(i) == n)
		{
			gui->game->objectives.setObjectiveFailed(i);
			break;
		}
	}
}

void Story::hintHidden(StoryContext* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game->gameHints.getNumberOfHints(); ++i)
	{
		if(gui->game->gameHints.getScriptNumber(i) == n)
		{
			gui->game->gameHints.setHintHidden(i);
			break;
		}
	}
}

void Story::hintVisible(StoryContext* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game->gameHints.getNumberOfHints(); ++i)
	{
		if(gui->game->gameHints.getScriptNumber(i) == n)
		{
			gui->game->gameHints.setHintVisible(i);
			break;
		}
	}
}

namespace
{
	/// One row of the script-name to GUI-object mapping used by highlightItem /
	/// unhighlightItem.
	struct HighlightItemName
	{
		const char* name;
		GameGUI::HighlightObject object;
	};

	/// The sole authority for which GUI item each script highlight name points at.
	/// The names are the SGSL surface — they appear verbatim in campaign scripts
	/// (`highlightItem("main menu icon")`), so they cannot be reworded. The draw sites
	/// in GameGUIDraw*.cpp find their arrow by looking the object back up in
	/// GameGUI::highlights, so each name must resolve to a distinct object; see
	/// highlightItemObjectsAreDistinct below.
	constexpr HighlightItemName highlightItemNames[] =
	{
		{ "main menu icon",              GameGUI::HighlightMainMenuIcon },
		{ "right side panel",            GameGUI::HighlightRightSidePanel },
		{ "under minimap icons",         GameGUI::HighlightUnderMinimapIcon },
		{ "units assigned bar",          GameGUI::HighlightUnitsAssignedBar },
		{ "units ratio bar",             GameGUI::HighlightRatioBar },
		{ "workers working free stat",   GameGUI::HighlightWorkersWorkingFreeStat },
		{ "explorers working free stat", GameGUI::HighlightExplorersWorkingFreeStat },
		{ "warriors working free stat",  GameGUI::HighlightWarriorsWorkingFreeStat },
		{ "forbidden zone on panel",     GameGUI::HighlightForbiddenZoneOnPanel },
		{ "guard zone on panel",         GameGUI::HighlightGuardZoneOnPanel },
		{ "clearing zone on panel",      GameGUI::HighlightClearingZoneOnPanel },
		{ "brush selector",              GameGUI::HighlightBrushSelector },
	};

	/// True when no two rows share a HighlightObject. A duplicate means a row was
	/// copy-pasted and kept the object it was copied from, which aims one script
	/// name at another name's arrow and leaves its own arrow unreachable.
	constexpr bool highlightItemObjectsAreDistinct()
	{
		for (std::size_t i = 0; i < std::size(highlightItemNames); ++i)
			for (std::size_t j = i + 1; j < std::size(highlightItemNames); ++j)
				if (highlightItemNames[i].object == highlightItemNames[j].object)
					return false;
		return true;
	}

	static_assert(highlightItemObjectsAreDistinct(),
		"two SGSL hilight item names resolve to the same GameGUI::HighlightObject");

	/// Resolves a script highlight item name to the GUI object it selects, or nullopt
	/// if the name is not highlightable. An unknown name is not an error — it is
	/// silently ignored, as it always has been.
	std::optional<GameGUI::HighlightObject> highlightObjectFromName(const std::string& name)
	{
		for (const HighlightItemName& entry : highlightItemNames)
		{
			if (name == entry.name)
				return entry.object;
		}
		return std::nullopt;
	}
}

void Story::setHighlightItem(StoryContext* gui, bool doSet)
{
	const std::string n = line[++lineSelector].msg;
	const std::optional<GameGUI::HighlightObject> object = highlightObjectFromName(n);
	if(!object)
		return;

	if(doSet)
	{
		gui->client->setHighlight(*object, true);
	}
	else
	{
		gui->client->setHighlight(*object, false);
	}
}

void Story::highlightItem(StoryContext* gui)
{
	setHighlightItem(gui, true);
}

void Story::unhighlightItem(StoryContext* gui)
{
	setHighlightItem(gui, false);
}

void Story::highlightUnits(StoryContext* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_WORKER;
	gui->client->setHighlight(GameGUI::HighlightWorkers+n, true);
}

void Story::unhighlightUnits(StoryContext* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_WORKER;
	gui->client->setHighlight(GameGUI::HighlightWorkers+n, false);
}

void Story::highlightBuildings(StoryContext* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->client->setHighlight(GameGUI::HighlightBuildingOnMap+n, true);
}

void Story::unhighlightBuildings(StoryContext* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->client->setHighlight(GameGUI::HighlightBuildingOnMap+n, false);
}

void Story::highlightBuildingOnPanel(StoryContext* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->client->setHighlight(GameGUI::HighlightBuildingOnPanel+n, true);
}

void Story::unhighlightBuildingOnPanel(StoryContext* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->client->setHighlight(GameGUI::HighlightBuildingOnPanel+n, false);
}

void Story::resetAI(StoryContext* gui)
{
	int player = line[++lineSelector].value;
	int aitype = line[++lineSelector].value;
	if(gui->game->players[player])
	{
		gui->game->players[player]->makeItAI(static_cast<AI::ImplementationID>(aitype));
	}
}


//! Enable or disable the GUI panel choice named by an SGSL object token, as
//! requested by the guiEnable / guiDisable script commands.
//!
//! The SGSL building/flag tokens are NOT laid out as one contiguous range:
//! building tokens straddle the flag tokens
//! (S_SWARM_B..S_DEFENCE_B, then S_EXPLOR_F..S_CLEARING_F, then S_WALL_B..S_MARKET_B).
//! Flags must therefore be matched before the "<= S_MARKET_B" buildings range,
//! otherwise flag tokens fall into the buildings branch and silently no-op
//! (a flag name is never found in the buildings choice list).
void Story::setGUIChoice(StoryContext* gui, SGSLToken::TokenType object, bool enable)
{
	if (object <= SGSLToken::S_WARRIOR)
	{
		// Units : TODO (no GUI panel choice for unit tokens yet)
	}
	else if (object >= SGSLToken::S_EXPLOR_F && object <= SGSLToken::S_CLEARING_F)
	{
		const std::string& flag = IntBuildingType::typeFromShortNumber(
			object - SGSLToken::S_EXPLOR_F + IntBuildingType::EXPLORATION_FLAG);
		if (enable)
			gui->client->enableFlagsChoice(flag);
		else
			gui->client->disableFlagsChoice(flag);
	}
	else if (object <= SGSLToken::S_MARKET_B)
	{
		const std::string& building = IntBuildingType::typeFromShortNumber(
			object - SGSLToken::S_SWARM_B);
		if (enable)
			gui->client->enableBuildingsChoice(building);
		else
			gui->client->disableBuildingsChoice(building);
	}
	else if (object <= SGSLToken::S_ALLIANCESCREEN)
	{
		const int element = object - SGSLToken::S_BUILDINGTAB;
		if (enable)
			gui->client->enableGUIElement(element);
		else
			gui->client->disableGUIElement(element);
	}
}


static const FunctionArgumentDescription totoDescription[] = {
	{ SGSLToken::S_WIN, SGSLToken::S_LOSE },
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription objectiveCompleteDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription objectiveHiddenDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription objectiveVisibleDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription objectiveFailedDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription hintHiddenDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription hintVisibleDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

static const FunctionArgumentDescription highlightItemDescription[] = {
	{ SGSLToken::STRING, SGSLToken::STRING },
	{ -1, -1}
};

static const FunctionArgumentDescription unhighlightItemDescription[] = {
	{ SGSLToken::STRING, SGSLToken::STRING },
	{ -1, -1}
};

static const FunctionArgumentDescription highlightUnitsDescription[] = {
	{ SGSLToken::S_WORKER, SGSLToken::S_WARRIOR },
	{ -1, -1}
};

static const FunctionArgumentDescription unhighlightUnitsDescription[] = {
	{ SGSLToken::S_WORKER, SGSLToken::S_WARRIOR },
	{ -1, -1}
};

static const FunctionArgumentDescription highlightBuildingsDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription unhighlightBuildingsDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription highlightBuildingOnPanelDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription unhighlightBuildingOnPanelDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription resetAIDescription[] = {
	{ SGSLToken::INT, SGSLToken::INT },
	{ SGSLToken::INT, SGSLToken::INT },
	{ -1, -1}
};

MapScriptSGSL::MapScriptSGSL()
{
	functions["toto"] = std::make_pair(totoDescription, &Story::toto);
	functions["objectiveHidden"] = std::make_pair(objectiveHiddenDescription, &Story::objectiveHidden);
	functions["objectiveVisible"] = std::make_pair(objectiveVisibleDescription, &Story::objectiveVisible);
	functions["objectiveComplete"] = std::make_pair(objectiveCompleteDescription, &Story::objectiveComplete);
	functions["objectiveFailed"] = std::make_pair(objectiveFailedDescription, &Story::objectiveFailed);
	functions["hintHidden"] = std::make_pair(hintHiddenDescription, &Story::hintHidden);
	functions["hintVisible"] = std::make_pair(hintVisibleDescription, &Story::hintVisible);
	functions["hilightItem"] = std::make_pair(highlightItemDescription, &Story::highlightItem);
	functions["unhilightItem"] = std::make_pair(unhighlightItemDescription, &Story::unhighlightItem);
	functions["hilightUnits"] = std::make_pair(highlightUnitsDescription, &Story::highlightUnits);
	functions["unhilightUnits"] = std::make_pair(unhighlightUnitsDescription, &Story::unhighlightUnits);
	functions["hilightBuildings"] = std::make_pair(highlightBuildingsDescription, &Story::highlightBuildings);
	functions["unhilightBuildings"] = std::make_pair(unhighlightBuildingsDescription, &Story::unhighlightBuildings);
	functions["hilightBuildingOnPanel"] = std::make_pair(highlightBuildingOnPanelDescription, &Story::highlightBuildingOnPanel);
	functions["unhilightBuildingOnPanel"] = std::make_pair(unhighlightBuildingOnPanelDescription, &Story::unhighlightBuildingOnPanel);
	functions["resetAI"] = std::make_pair(resetAIDescription, &Story::resetAI);
}
