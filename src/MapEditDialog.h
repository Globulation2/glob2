/*
  Copyright (C) 2006-2008 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef MapEditDialog_h
#define MapEditDialog_h

#include "GUIBase.h"
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

///This is the map editor menu screen. It has 5 buttons. Its very similair to the in-game main menu
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


const int NumberOfPlayerSelectors = 12;

///This is the teams editor screen. This is the editor that allows the map creator to choose alliances and arrange teams
///in the map. This is primarily for campaign missions since these settings are overridden for custom games
class TeamsEditor : public OverlayScreen
{
public:
	TeamsEditor(Game* game);
	virtual ~TeamsEditor() { }
	void onAction(Widget *source, Action action, int par1, int par2);
	void generateGameHeader();

	enum
	{
		OK,
		CANCEL
	};
private:
	Game* game;

	//! Player enable/disable buttons
	OnOffButton *isPlayerActive[Team::MAX_COUNT];
	///List of the player names
	Text* playerName[Team::MAX_COUNT];
	//! Player colors
	ColorButton *color[Team::MAX_COUNT];
	//! Player ally temas
	MultiTextButton *allyTeamNumbers[Team::MAX_COUNT];
	//! Multi-text button containing an aiSelector
	MultiTextButton *aiSelector[Team::MAX_COUNT];
};

#endif
