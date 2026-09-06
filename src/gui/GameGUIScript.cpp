// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"

void GameGUI::enableBuildingsChoice(const std::string &name)
{
	for (size_t i=0; i<buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i])
			buildingsChoiceState[i] = true;
	}
}

void GameGUI::disableBuildingsChoice(const std::string &name)
{
	for (size_t i=0; i<buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i])
			buildingsChoiceState[i] = false;
	}
}

bool GameGUI::isBuildingEnabled(const std::string &name)
{
	for (size_t i=0; i<buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i])
                  return buildingsChoiceState[i];
	}
	assert (false);
}

void GameGUI::enableFlagsChoice(const std::string &name)
{
	for (size_t i=0; i<flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i])
			flagsChoiceState[i] = true;
	}
}

void GameGUI::disableFlagsChoice(const std::string &name)
{
	for (size_t i=0; i<flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i])
			flagsChoiceState[i] = false;
	}
}

bool GameGUI::isFlagEnabled(const std::string &name)
{
	for (size_t i=0; i<flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i])
                  return flagsChoiceState[i];
	}
        assert (false);
}

void GameGUI::enableGUIElement(int id)
{
	hiddenGUIElements &= ~(1<<id);
}

void GameGUI::disableGUIElement(int id)
{
	if (globalContainer->replaying) return;

	hiddenGUIElements |= (1<<id);
	if (displayMode==id)
		nextDisplayMode();
}

void GameGUI::showScriptText(const std::string &text)
{
	scriptText = text;
	scriptTextUpdated = true;
}

void GameGUI::showScriptTextTr(const std::string &text, const std::string &lang)
{
	if (lang == globalContainer->settings.language)
		showScriptText(text);
}

void GameGUI::hideScriptText()
{
	scriptText.clear();
}

void GameGUI::setCpuLoad(int s)
{
	smoothedCPULoad[smoothedCPUPos]=s;
	smoothedCPUPos=(smoothedCPUPos+1) % SMOOTHED_CPU_SIZE;
}



void GameGUI::setCampaignGame(Campaign& campaign, const std::string& missionName)
{
	this->campaign=&campaign;
	this->missionName=missionName;
}



void GameGUI::updateHilightInGame()
{
	game.highlightUnitType = 0;
	if(hilights.find(HilightWorkers) != hilights.end())
	{
		game.highlightUnitType |= 1<<WORKER;
	}
	if(hilights.find(HilightExplorers) != hilights.end())
	{
		game.highlightUnitType |= 1<<EXPLORER;
	}
	if(hilights.find(HilightWarriors) != hilights.end())
	{
		game.highlightUnitType |= 1<<WARRIOR;
	}

	game.highlightBuildingType = 0;

	for(int i=0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		if(hilights.find(HilightBuildingOnMap + i) != hilights.end())
		{
			game.highlightBuildingType |= 1<<(i);
		}
	}
}
