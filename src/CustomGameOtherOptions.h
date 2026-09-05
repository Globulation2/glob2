// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <array>

#include "AI.h"
#include "Glob2Screen.h"
#include "GameHeader.h"
#include "MapHeader.h"
#include "Team.h"

using namespace GAGGUI;

namespace GAGGUI
{
	class Button;
	class TextButton;
	class OnOffButton;
	class ColorButton;
	class MultiTextButton;
	class Text;
	class Number;
}

/// This screen is used to set the other settings, like alliances, for the game
class CustomGameOtherOptions : public Glob2Screen
{

public:
	/// Constructor, edits the given game header and map header
	CustomGameOtherOptions(GameHeader& gameHeader, MapHeader& mapHeader, bool readOnly);
	/// Destructor
	virtual ~CustomGameOtherOptions();
	///Recieves an action from a widget
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	
	///These are the end values for this screen
	enum EndValues
	{
		Finished,
		Canceled,
	};
	
private:
	enum
	{
		OK,
		CANCEL,
		TEAMSFIXED,
		PRESTIGEWINENABLED,
		MAPDISCOVERED,
	};
	
	///"Other Options" Title
	Text* title;
	///Ok button
	TextButton* ok;
	///Cancel button
	TextButton* cancel;
	
	///List of the player names
	std::array<Text*, Team::MAX_COUNT> playerNames{};
	//! Player colors
	std::array<ColorButton*, Team::MAX_COUNT> color{};
	//! Player ally teams
	std::array<MultiTextButton*, Team::MAX_COUNT> allyTeamNumbers{};

	///Button fixing teams during the match
	OnOffButton *teamsFixed;
	///Text for above button
	Text* teamsFixedText;
	
	///Enables a win via prestige
	OnOffButton* prestigeWinEnabled;
	///Text for above button
	Text* prestigeWinEnabledText;

	///Button to set the map being already discovered
	OnOffButton *mapDiscovered;
	///Text for above button
	Text* mapDiscoveredText;
	
	///This updates the winning conditions in the game header
	void updateGameHeaderWinningConditions();
	///This updates the screens winning conditions from the game header
	void updateScreenWinningConditions();
	
	GameHeader& gameHeader;
	GameHeader oldGameHeader;
};

