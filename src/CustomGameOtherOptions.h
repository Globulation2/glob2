/*
  Copyright (C) 2008 Bradley Arsenault

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

#ifndef CustomGameOtherOptions_h
#define CustomGameOtherOptions_h

#include "AI.h"
#include "Glob2Screen.h"
#include "GameHeader.h"
#include "MapHeader.h"

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
	CustomGameOtherOptions(GameHeader& gameHeader, MapHeader& mapHeader);
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
	};
	
	///"Other Options" Title
	Text* title;
	///Ok button
	TextButton* ok;
	///Cancel button
	TextButton* cancel;
	
	///List of the player names
	Text* playerNames[32];
	//! Player colors
	ColorButton *color[32];
	//! Player ally temas
	MultiTextButton *allyTeamNumbers[32];

	///Button fixing teams during the match
	OnOffButton *teamsFixed;
	///Text for above button
	Text* teamsFixedText;
	
	///Enables a win via prestige
	OnOffButton* prestigeWinEnabled;
	///Text for above button
	Text* prestigeWinEnabledText;
	
	///This updates the winning conditions in the game header
	void updateGameHeaderWinningConditions();
	///This updates the screens winning conditions from the game header
	void updateScreenWinningConditions();
	
	
	MapHeader& mapHeader;
	GameHeader& gameHeader;
	GameHeader oldGameHeader;
};

#endif
