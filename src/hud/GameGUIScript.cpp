// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "render/scene/BuildingCatalogView.h"

namespace
{
const std::string& choiceAlias(const std::shared_ptr<const std::vector<BuildingType>>& catalog, const std::string& key)
{
    static const std::string empty;
    if (!catalog) return empty;
    const BuildingCatalogView view(*catalog);
    const auto* type = view.get(view.findByKey(key));
    return type ? type->type : empty;
}
}


void GameGUI::enableBuildingsChoice(const std::string &name)
{
	for (size_t i = 0; i < buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i] || name == choiceAlias(choiceCatalog, buildingsChoiceName[i]))
			buildingsChoiceState[i] = true;
	}
}

void GameGUI::disableBuildingsChoice(const std::string &name)
{
	for (size_t i = 0; i < buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i] || name == choiceAlias(choiceCatalog, buildingsChoiceName[i]))
			buildingsChoiceState[i] = false;
	}
}

bool GameGUI::isBuildingEnabled(const std::string &name)
{
	for (size_t i = 0; i < buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i] || name == choiceAlias(choiceCatalog, buildingsChoiceName[i]))
			return buildingsChoiceState[i];
	}
	return false;
}

void GameGUI::enableFlagsChoice(const std::string &name)
{
	for (size_t i = 0; i < flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i] || name == choiceAlias(choiceCatalog, flagsChoiceName[i]))
			flagsChoiceState[i] = true;
	}
}

void GameGUI::disableFlagsChoice(const std::string &name)
{
	for (size_t i = 0; i < flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i] || name == choiceAlias(choiceCatalog, flagsChoiceName[i]))
			flagsChoiceState[i] = false;
	}
}

bool GameGUI::isFlagEnabled(const std::string &name)
{
	for (size_t i = 0; i < flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i] || name == choiceAlias(choiceCatalog, flagsChoiceName[i]))
			return flagsChoiceState[i];
	}
	return false;
}

void GameGUI::enableGUIElement(int id)
{
	if (id < 0 || id >= 32) return;
	hiddenGUIElements &= ~(Uint32(1) << id);
}

void GameGUI::disableGUIElement(int id)
{
	if (globalContainer->replaying)
		return;

	if (id < 0 || id >= 32) return;
	hiddenGUIElements |= (Uint32(1) << id);
	if (displayMode == id)
		nextDisplayMode();
}

void GameGUI::setHighlight(int highlight, bool on)
{
	if (on)
		highlights.insert(highlight);
	else
		highlights.erase(highlight);
}

void GameGUI::showScriptText(const std::string &text)
{
	scriptText = text;
	scriptTextUpdated = true;
}

void GameGUI::setScriptPresentationText(std::string text, bool publishHistory)
{
	if (scriptText != text)
	{
		scriptText.swap(text);
		scriptTextUpdated = publishHistory && !scriptText.empty();
	}
}

void GameGUI::showScriptTextTr(const std::string &text, const std::string &lang)
{
	if (lang == globalContainer->settings.language)
		showScriptText(text);
}

void GameGUI::hideScriptText()
{
	scriptText.clear();
	scriptTextUpdated = false;
}

void GameGUI::setCampaignGame(Campaign &campaign, const std::string &missionName)
{
	this->campaign = &campaign;
	this->missionName = missionName;
}

void GameGUI::startScriptClientChannel()
{
	const auto choices = [&](const auto& names, const auto& states) {
		std::vector<ScriptClientChannel::Choice> result;
		for (size_t i = 0; i < names.size(); ++i)
			result.push_back({names[i], choiceAlias(choiceCatalog, names[i]), states[i]});
		return result;
	};
	game.scriptClient.start(clientEvents, choices(buildingsChoiceName, buildingsChoiceState), choices(flagsChoiceName, flagsChoiceState));
}
