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
#include "Player.h"
#include "SGSL.h"

void Story::toto(GameGUI* gui)
{
	std::cout << "toto func : ";
	std::cout << SGSLToken::getNameByType(line[++lineSelector].type) << " ";
	std::cout << line[++lineSelector].value << "\n";
}

void Story::objectiveHidden(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game.objectives.getScriptNumber(i) == n)
		{
			gui->game.objectives.setObjectiveHidden(i);
			break;
		}
	}
}

void Story::objectiveVisible(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game.objectives.getScriptNumber(i) == n)
		{
			gui->game.objectives.setObjectiveVisible(i);
			break;
		}
	}
}

void Story::objectiveComplete(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game.objectives.getScriptNumber(i) == n)
		{
			gui->game.objectives.setObjectiveComplete(i);
			break;
		}
	}
}

void Story::objectiveFailed(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if(gui->game.objectives.getScriptNumber(i) == n)
		{
			gui->game.objectives.setObjectiveFailed(i);
			break;
		}
	}
}

void Story::hintHidden(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.gameHints.getNumberOfHints(); ++i)
	{
		if(gui->game.gameHints.getScriptNumber(i) == n)
		{
			gui->game.gameHints.setHintHidden(i);
			break;
		}
	}
}

void Story::hintVisible(GameGUI* gui)
{
	int n = line[++lineSelector].value;
	for(int i=0; i<gui->game.gameHints.getNumberOfHints(); ++i)
	{
		if(gui->game.gameHints.getScriptNumber(i) == n)
		{
			gui->game.gameHints.setHintVisible(i);
			break;
		}
	}
}

namespace
{
	/// One row of the script-name to GUI-object mapping used by hilightItem /
	/// unhilightItem.
	struct HilightItemName
	{
		const char* name;
		GameGUI::HilightObject object;
	};

	/// The sole authority for which GUI item each script hilight name points at.
	/// The names are the SGSL surface — they appear verbatim in campaign scripts
	/// (`hilightItem("main menu icon")`), so they cannot be reworded. The draw sites
	/// in GameGUIDraw*.cpp find their arrow by looking the object back up in
	/// GameGUI::hilights, so each name must resolve to a distinct object; see
	/// hilightItemObjectsAreDistinct below.
	constexpr HilightItemName hilightItemNames[] =
	{
		{ "main menu icon",              GameGUI::HilightMainMenuIcon },
		{ "right side panel",            GameGUI::HilightRightSidePanel },
		{ "under minimap icons",         GameGUI::HilightUnderMinimapIcon },
		{ "units assigned bar",          GameGUI::HilightUnitsAssignedBar },
		{ "units ratio bar",             GameGUI::HilightRatioBar },
		{ "workers working free stat",   GameGUI::HilightWorkersWorkingFreeStat },
		{ "explorers working free stat", GameGUI::HilightExplorersWorkingFreeStat },
		{ "warriors working free stat",  GameGUI::HilightWarriorsWorkingFreeStat },
		{ "forbidden zone on panel",     GameGUI::HilightForbiddenZoneOnPanel },
		{ "guard zone on panel",         GameGUI::HilightGuardZoneOnPanel },
		{ "clearing zone on panel",      GameGUI::HilightClearingZoneOnPanel },
		{ "brush selector",              GameGUI::HilightBrushSelector },
	};

	/// True when no two rows share a HilightObject. A duplicate means a row was
	/// copy-pasted and kept the object it was copied from, which aims one script
	/// name at another name's arrow and leaves its own arrow unreachable.
	constexpr bool hilightItemObjectsAreDistinct()
	{
		for (std::size_t i = 0; i < std::size(hilightItemNames); ++i)
			for (std::size_t j = i + 1; j < std::size(hilightItemNames); ++j)
				if (hilightItemNames[i].object == hilightItemNames[j].object)
					return false;
		return true;
	}

	static_assert(hilightItemObjectsAreDistinct(),
		"two SGSL hilight item names resolve to the same GameGUI::HilightObject");

	/// Resolves a script hilight item name to the GUI object it selects, or nullopt
	/// if the name is not hilightable. An unknown name is not an error — it is
	/// silently ignored, as it always has been.
	std::optional<GameGUI::HilightObject> hilightObjectFromName(const std::string& name)
	{
		for (const HilightItemName& entry : hilightItemNames)
		{
			if (name == entry.name)
				return entry.object;
		}
		return std::nullopt;
	}
}

void Story::setHighlightItem(GameGUI* gui, bool doSet)
{
	const std::string n = line[++lineSelector].msg;
	const std::optional<GameGUI::HilightObject> object = hilightObjectFromName(n);
	if(!object)
		return;

	if(doSet)
	{
		gui->hilights.insert(*object);
	}
	else
	{
		gui->hilights.erase(*object);
	}
}

void Story::hilightItem(GameGUI* gui)
{
	setHighlightItem(gui, true);
}

void Story::unhilightItem(GameGUI* gui)
{
	setHighlightItem(gui, false);
}

void Story::hilightUnits(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_WORKER;
	gui->hilights.insert(GameGUI::HilightWorkers+n);
}

void Story::unhilightUnits(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_WORKER;
	gui->hilights.erase(GameGUI::HilightWorkers+n);
}

void Story::hilightBuildings(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->hilights.insert(GameGUI::HilightBuildingOnMap+n);
}

void Story::unhilightBuildings(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->hilights.erase(GameGUI::HilightBuildingOnMap+n);
}

void Story::hilightBuildingOnPanel(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->hilights.insert(GameGUI::HilightBuildingOnPanel+n);
}

void Story::unhilightBuildingOnPanel(GameGUI* gui)
{
	int n = line[++lineSelector].type - SGSLToken::S_SWARM_B;
	gui->hilights.erase(GameGUI::HilightBuildingOnPanel+n);
}

void Story::resetAI(GameGUI* gui)
{
	int player = line[++lineSelector].value;
	int aitype = line[++lineSelector].value;
	if(gui->game.players[player])
	{
		gui->game.players[player]->makeItAI(static_cast<AI::ImplementitionID>(aitype));
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
void Story::setGUIChoice(GameGUI* gui, SGSLToken::TokenType object, bool enable)
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
			gui->enableFlagsChoice(flag);
		else
			gui->disableFlagsChoice(flag);
	}
	else if (object <= SGSLToken::S_MARKET_B)
	{
		const std::string& building = IntBuildingType::typeFromShortNumber(
			object - SGSLToken::S_SWARM_B);
		if (enable)
			gui->enableBuildingsChoice(building);
		else
			gui->disableBuildingsChoice(building);
	}
	else if (object <= SGSLToken::S_ALLIANCESCREEN)
	{
		const int element = object - SGSLToken::S_BUILDINGTAB;
		if (enable)
			gui->enableGUIElement(element);
		else
			gui->disableGUIElement(element);
	}
}


static const FunctionArgumentDescription totoDescription[] = {
	{ SGSLToken::S_WIN, SGSLToken::S_LOOSE },
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

static const FunctionArgumentDescription hilightItemDescription[] = {
	{ SGSLToken::STRING, SGSLToken::STRING },
	{ -1, -1}
};

static const FunctionArgumentDescription unhilightItemDescription[] = {
	{ SGSLToken::STRING, SGSLToken::STRING },
	{ -1, -1}
};

static const FunctionArgumentDescription hilightUnitsDescription[] = {
	{ SGSLToken::S_WORKER, SGSLToken::S_WARRIOR },
	{ -1, -1}
};

static const FunctionArgumentDescription unhilightUnitsDescription[] = {
	{ SGSLToken::S_WORKER, SGSLToken::S_WARRIOR },
	{ -1, -1}
};

static const FunctionArgumentDescription hilightBuildingsDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription unhilightBuildingsDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription hilightBuildingOnPanelDescription[] = {
	{ SGSLToken::S_SWARM_B, SGSLToken::S_MARKET_B },
	{ -1, -1}
};

static const FunctionArgumentDescription unhilightBuildingOnPanelDescription[] = {
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
	functions["hilightItem"] = std::make_pair(hilightItemDescription, &Story::hilightItem);
	functions["unhilightItem"] = std::make_pair(unhilightItemDescription, &Story::unhilightItem);
	functions["hilightUnits"] = std::make_pair(hilightUnitsDescription, &Story::hilightUnits);
	functions["unhilightUnits"] = std::make_pair(unhilightUnitsDescription, &Story::unhilightUnits);
	functions["hilightBuildings"] = std::make_pair(hilightBuildingsDescription, &Story::hilightBuildings);
	functions["unhilightBuildings"] = std::make_pair(unhilightBuildingsDescription, &Story::unhilightBuildings);
	functions["hilightBuildingOnPanel"] = std::make_pair(hilightBuildingOnPanelDescription, &Story::hilightBuildingOnPanel);
	functions["unhilightBuildingOnPanel"] = std::make_pair(unhilightBuildingOnPanelDescription, &Story::unhilightBuildingOnPanel);
	functions["resetAI"] = std::make_pair(resetAIDescription, &Story::resetAI);
}
