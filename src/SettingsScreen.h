/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __SETTINGSSCREEN_H
#define __SETTINGSSCREEN_H

#include "Glob2Screen.h"
#include "Settings.h"
#include <string>

namespace GAGGUI
{
	class List;
	class TextInput;
	class TextButton;
	class OnOffButton;
	class Text;
	class Selector;
	class Number;
}

class SettingsScreen : public Glob2Screen
{
public:
	enum
	{
		OK = 1,
		CANCEL = 2,
		FULLSCREEN = 3,
		USEGL = 4,
		LOWQUALITY = 5,
		CUSTOMCUR = 6,
		MUTE = 7,
		REMEMBERUNIT = 8,
		GENERALSETTINGS = 9,
		UNITSETTINGS = 10,
		KEYBOARDSETTINGS = 11,
		RESTOREDEFAULTSHORTCUTS=12,
		GAMESHORTCUTS=13,
		EDITORSHORTCUTS=14,
	};
private:
	Settings old_settings;
	List *languageList;
	List *modeList;
	TextInput *userName;
	
	TextButton *ok, *cancel, *generalsettings, *unitsettings, *keyboardsettings;
	OnOffButton *fullscreen, *usegpu, *lowquality, *customcur;
	Selector *musicVol;
	OnOffButton *audioMute, *rememberUnitButton;
	Number *swarmUnitRatio0c, *swarmUnitRatio0, *innUnitRatio0c, *innUnitRatio0, *innUnitRatio1c, *innUnitRatio1, *innUnitRatio2c, *innUnitRatio2, *hospitalUnitRatio0c, *hospitalUnitRatio1c, *hospitalUnitRatio2c, *racetrackUnitRatio0c, *racetrackUnitRatio1c, *racetrackUnitRatio2c, *swimmingpoolUnitRatio0c, *swimmingpoolUnitRatio1c, *swimmingpoolUnitRatio2c, *barracksUnitRatio0c, *barracksUnitRatio1c, *barracksUnitRatio2c, *schoolUnitRatio0c, *schoolUnitRatio1c, *schoolUnitRatio2c, *defencetowerUnitRatio0c, *defencetowerUnitRatio0, *defencetowerUnitRatio1c, *defencetowerUnitRatio1, *defencetowerUnitRatio2c, *defencetowerUnitRatio2, *stonewallUnitRatio0c, *marketUnitRatio0c, *warflagUnitRatio, *clearflagUnitRatio, *exploreflagUnitRatio;
//	Text *title;
	Text *language, *display, *usernameText, *audio;
	Text *fullscreenText, *usegpuText, *lowqualityText, *customcurText, *musicVolText, *audioMuteText, *rememberUnitText;
	Text *swarmUnitText0c, *swarmUnitText0, *innUnitText0c, *innUnitText0, *innUnitText1c, *innUnitText1, *innUnitText2c, *innUnitText2, *hospitalUnitText0c, *hospitalUnitText1c, *hospitalUnitText2c, *racetrackUnitText0c, *racetrackUnitText1c, *racetrackUnitText2c, *swimmingpoolUnitText0c, *swimmingpoolUnitText1c, *swimmingpoolUnitText2c, *barracksUnitText0c, *barracksUnitText1c, *barracksUnitText2c, *schoolUnitText0c, *schoolUnitText1c, *schoolUnitText2c, *defencetowerUnitText0c, *defencetowerUnitText0, *defencetowerUnitText1c, *defencetowerUnitText1, *defencetowerUnitText2c, *defencetowerUnitText2, *stonewallUnitText0c, *marketUnitText0c, *warflagUnitText, *clearflagUnitText, *exploreflagUnitText;
	Text *actDisplay;
	Text *rebootWarning;

	TextButton* game_shortcuts;
	TextButton* editor_shortcuts;
	List* keyboard_shortcut_names;
	std::vector<std::string> internal_names;
	List* keyboard_shortcuts;
	std::vector<std::string> shortcut_actions;
	std::vector<std::string> shortcut_names;
	List* editor_keyboard_shortcuts;
	std::vector<std::string> editor_shortcut_actions;
	std::vector<std::string> editor_shortcut_names;
	TextButton* restore_default_shortcuts;
	void reset_names();

	bool gfxAltered;
	
	//! If GL is enabled, hide useless options
	void setVisibilityFromGraphicType(void);
	//! If mute is set, do not show volume slider
	void setVisibilityFromAudioSettings(void);
	//! reset res and redraw everything
	void updateGfxCtx(void);
	//! Return a string representing the actual display mode
	std::string actDisplayModeToString(void);

public:
	SettingsScreen();
	virtual ~SettingsScreen() { }
	void onAction(Widget *source, Action action, int par1, int par2);
	static int menu(void);
};

#endif
