// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006-2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "GUIBase.h"
#include "Team.h"
#include <string>

namespace GAGCore
{
	class Sprite;
	class Font;
}
using namespace GAGCore;
namespace GAGGUI
{
	class OverlayScreen;
	class TextButton;
	class Text;
	class TextInput;
	class ColorButton;
	class MultiTextButton;
}
using namespace GAGGUI;
class Unit;

class Game;

///This is the map editor menu screen. It has 5 buttons. Its very similar to the in-game main menu
class MapEditMenuScreen : public OverlayScreen
{
public:
	MapEditMenuScreen();
	virtual ~MapEditMenuScreen() { }
	void onAction(Widget *source, Action action, int par1, int par2);

	enum
	{
		LOAD_MAP,
		SAVE_MAP,
		OPEN_SCRIPT_EDITOR,
		OPEN_TEAMS_EDITOR,
		RETURN_EDITOR,
		QUIT_EDITOR
	};
};


///This is a text info box. It is used primarily for entering the names of the script areas,
///however it is generic enough to be recycled for other purposes.
class AskForTextInput : public OverlayScreen
{
public:
	enum
	{
		OK,
		CANCEL
	};
	AskForTextInput(const std::string& label, const std::string& current);
	void onAction(Widget *source, Action action, int par1, int par2);
	std::string getText();
private:
	TextInput* textEntry;
	TextButton* ok;
	TextButton* cancel;
	Text* label;
	std::string labelText;
	std::string currentText;
};


///This is the teams editor screen. This is the editor that allows the map creator to choose alliances and arrange teams
///in the map. This is primarily for campaign missions since these settings are overridden for custom games
class TeamsEditor : public OverlayScreen
{
public:
    std::string phoneLabel(Widget* widget) const;
	TeamsEditor(Game* game);
	virtual ~TeamsEditor() { }
	void onAction(Widget *source, Action action, int par1, int par2);
	///Rebuilds the game's GameHeader from scratch out of the widget state
	///(it does not mutate the existing header). Active slots are packed
	///into consecutive player numbers; each slot's ally-team widget index
	///(0-based) is written back as ally team number widgetIndex + 1, the
	///inverse of allyTeamNumberToWidgetIndex().
	void generateGameHeader();

	enum
	{
		OK,
		CANCEL
	};

	///Widget return codes are base + slot index, one base per widget row
	///kind, so onAction can recover the slot from the code.
	static constexpr int PLAYER_ACTIVE_BASE = 100;
	static constexpr int COLOR_BASE = 200;
	static constexpr int AI_SELECTOR_BASE = 300;
	static constexpr int ALLY_TEAM_BASE = 400;
private:
	Game* game;
	
	//! Player enable/disable buttons
	OnOffButton *isPlayerActive[Team::MAX_COUNT];
	///List of the player names
	Text* playerName[Team::MAX_COUNT];
	//! Player colors
	ColorButton *color[Team::MAX_COUNT];
	//! Player ally teams
	MultiTextButton *allyTeamNumbers[Team::MAX_COUNT];
	//! Multi-text button containing an aiSelector
	MultiTextButton *aiSelector[Team::MAX_COUNT];
};
