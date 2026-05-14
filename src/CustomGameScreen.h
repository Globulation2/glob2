// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "AI.h"
#include "ChooseMapScreen.h"
#include "Team.h"
#include <GUIBase.h>
using namespace GAGGUI;

namespace GAGGUI
{
	class Button;
	class TextButton;
	class OnOffButton;
	class ColorButton;
	class MultiTextButton;
	class Text;
}
class Glob2FileList;
class MapPreview;

//! This screen is used to setup a custom game. AI can be set. Map choosing functionnalities are inherited from ChooseMapScreen
class CustomGameScreen : public ChooseMapScreen
{

public:
	//! Constructor, builds a ChooseMapScreen for maps
	CustomGameScreen();
	//! Destructor
	virtual ~CustomGameScreen();
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	virtual void validMapSelectedhandler(void);
	//! Returns true if AI i is enabled
	bool isActive(int i);
	//! Returns the implementation of AI i. If AI is disabled, result is undefined
	AI::ImplementitionID getAiImplementation(int i);
	//! Returns the color of AI i. If AI is disabled, result is undefined
	int getSelectedColor(int i);

private:


	///Updates the gameHeader with the chosen players for the map
	void updatePlayers();

	//! Player enable/disable buttons
	OnOffButton *isPlayerActive[Team::MAX_COUNT];
	//! Team color buttons
	ColorButton *color[Team::MAX_COUNT];
	//! Text shown when entry is disabled
	Text *closedText[Team::MAX_COUNT];
	//! Multi-text button containing names of available Players
	MultiTextButton *aiSelector[Team::MAX_COUNT];
	//! Text button that links to the custom game other settings screen
	TextButton* otherOptions;
	//! Text button that links to the ai descriptions
	TextButton* aiDescriptions;

};

