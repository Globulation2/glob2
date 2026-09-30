// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
#include "CustomGameOtherOptions.h"
#include "AllyTeamWidgetIndex.h"

using namespace Glob2UI;

CustomGameOtherOptions::CustomGameOtherOptions(GameHeader &gameHeader, MapHeader &mapHeader, bool readOnly)
	: gameHeader(gameHeader), mapHeader(mapHeader), oldGameHeader(gameHeader), readOnly(readOnly)
{
}

void CustomGameOtherOptions::onEscape()
{
	if (readOnly)
		endExecute(Finished);
	else
	{
		gameHeader = oldGameHeader;
		endExecute(Canceled);
	}
}

bool CustomGameOtherOptions::prestigeWinEnabled() const
{
	for (const auto &condition : gameHeader.getWinningConditions())
		if (condition->getType() == WCPrestige)
			return true;
	return false;
}

void CustomGameOtherOptions::setAllyTeam(int player, int widgetIndex)
{
	const int team = gameHeader.getBasePlayer(player).teamNumber;
	// Widget indices are 0-based; ally team numbers are 1-based.
	gameHeader.setAllyTeamNumber(team, widgetIndex + 1);
}

Element CustomGameOtherOptions::build(const Presentation &p)
{
	std::vector<std::string> teamChoices;
	for (int j = 0; j < mapHeader.getNumberOfTeams(); ++j)
		teamChoices.push_back(std::to_string(j + 1));
	std::vector<Element> players;
	for (int i = 0; i < gameHeader.getNumberOfPlayers(); ++i)
	{
		const auto &player = gameHeader.getBasePlayer(i);
		const auto color = mapHeader.getBaseTeam(player.teamNumber).color;
		const int allyIndex = allyTeamNumberToWidgetIndex(gameHeader.getAllyTeamNumber(player.teamNumber), mapHeader.getNumberOfTeams());
		ChoiceOptions options;
		options.controlEnabled = !readOnly;
		players.push_back(row({swatch(color), expanded(label(player.name)),
							   width(p.pt(96), choice("ally/" + std::to_string(i), teamChoices, allyIndex,
													  [this, i](int v) { setAllyTeam(i, v); }, options))},
							  {-1, CrossAlign::Center}));
	}
	std::vector<Element> optionRows{toggle("teams-fixed", tr("[Teams Fixed]"), gameHeader.areAllyTeamsFixed(),
										   [this](bool v) { gameHeader.setAllyTeamsFixed(v); }, !readOnly),
									toggle("prestige", tr("[Prestige Win Enabled]"), prestigeWinEnabled(),
										   [this](bool v) { WinningCondition::setPrestigeWinCondition(gameHeader.getWinningConditions(), v); }, !readOnly),
									toggle("discovered", tr("[Map Discovered]"), gameHeader.isMapDiscovered(),
										   [this](bool v) { gameHeader.setMapDiscovered(v); }, !readOnly)};
	// Read-only: the host's Settings > Experiments decide these.
	if (!gameHeader.getExperiments().empty())
		optionRows.push_back(caption(tr("[Experiments set by the host]") + ": " + experimentLabelList(gameHeader.getExperiments())));
	auto options = column(std::move(optionRows));
	auto playersColumn = column(std::move(players));
	Element body = adaptive(
		[playersColumn, options](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(640))
				return scroll("options/scroll", column({playersColumn, divider(), options}));
			return scroll("options/scroll", row({expanded(playersColumn), expanded(options)}, {-1, CrossAlign::Start}));
		});
	std::vector<MenuAction> buttons{{"ok", tr("[ok]"), [this] { endExecute(Finished); }, true, SDLK_RETURN}};
	if (!readOnly)
		buttons.push_back({"cancel", tr("[Cancel]"), [this] { onEscape(); }, false, SDLK_ESCAPE});
	return page(tr("[Other Options]"), body, actions(std::move(buttons), p), p, 800);
}
