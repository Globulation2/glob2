/*
  Copyright (C) 2007 Bradley Arsenault
  
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

#ifndef __SETTINGS_SCREEN_H
#define __SETTINGS_SCREEN_H

#include "Glob2Screen.h"
#include "Settings.h"
#include <string>

#include "KeyboardManager.h"
#include "GUIKeySelector.h"

namespace GAGGUI
{
	class List;
	class TextInput;
	class TextButton;
	class OnOffButton;
	class MultiTextButton;
	class Text;
	class Selector;
	class Number;
}

class SettingsScreen : public Glob2TabScreen
{
public:
	enum
	{
		OK = 1,
		CANCEL = 2,
		FULLSCREEN = 3,
		USE_GL = 4,
		LOW_QUALITY = 5,
		CUSTOM_CURSOR = 6,
		MUTE = 7,
		REMEMBER_UNIT = 8,
		GENERAL_SETTINGS = 9,
		UNIT_SETTINGS = 10,
		KEYBOARD_SETTINGS = 11,
		RESTORE_DEFAULT_SHORTCUTS=12,
		GAME_SHORTCUTS=13,
		EDITOR_SHORTCUTS=14,
		SECOND_KEY=15,
		PRESSED_SELECTOR=15,
		ADD_SHORTCUT=16,
		REMOVE_SHORTCUT=17,
		SCROLLWHEEL=18,
		BUILDING_SETTINGS=19,
		CONSTRUCTION_SITES=20,
		UPGRADES=21,
		FLAG_SETTINGS=22,
	};
private:
	Settings old_settings;
	List *languageList;
	List *modeList;
	Text *modeListNote;
	TextInput *userName;
	
	TextButton *ok, *cancel;
	TextButton *buildings, *flags, *constructionSites, *upgrades;
	OnOffButton *fullscreen, *useGpu, *lowQuality, *customCursor, *scrollwheel;
	Selector *musicVol;
	Selector *voiceVol;
	OnOffButton *audioMute, *rememberUnitButton;
	Number* unitRatios[IntBuildingType::NB_BUILDING][6];
	Text* unitRatioTexts[IntBuildingType::NB_BUILDING][6];
	int unitRatioGroupNumbers[IntBuildingType::NB_BUILDING][6];
	Number* flagRadii[3];
	Text* flagRadiusTexts[3];
	int flagRadiusGroupNumbers[3];
//	Text *title;
	Text *language, *display, *usernameText, *audio;
	Text *fullscreenText, *useGpuText, *lowQualityText, *customCursorText, *musicVolText, *audioMuteText, *voiceVolText, *rememberUnitText, *scrollwheelText;
	Text *actDisplay;
	Text *rebootWarning;

	void addNumbersFor(int low, int high, Number* widget);

	TextButton* game_shortcuts;
	TextButton* editor_shortcuts;
	TextButton* restore_default_shortcuts;

	List* shortcut_list;
	KeySelector* select_key_1;
	OnOffButton *key_2_active;
	KeySelector* select_key_2;
	MultiTextButton* pressedUnpressedSelector;
	List* action_list;
	TextButton* add_shortcut;
	TextButton* remove_shortcut;
	
	Text* unitSettingsExplanation;
	Text* flagSettingsExplanation;
	
	bool gfxAltered;
	
	//! If GL is enabled, hide useless options
	void setVisibilityFromGraphicType(void);
	//! If mute is set, do not show volume slider
	void setVisibilityFromAudioSettings(void);
	//! reset res and redraw everything
	void updateGfxCtx(void);
	//! Return a string representing the actual display mode
	std::string actDisplayModeToString(void);
	///processes a potential change in the selected fullscreen state
	void setFullscreen(void);

	///Holds the keyboard layout for the map editor
	KeyboardManager mapEditKeyboardManager;
	///Holds the keyboard layout for the game gui
	KeyboardManager guiKeyboardManager;
public:
	int generalGroup;
	int unitGroup;
	int keyboardGroup;

	ShortcutMode currentMode;
	///Quick code that adds in a default unit assignment widget pair at the specific position, and returns the width.
	int addDefaultUnitAssignmentWidget(int type, int level, int x, int y, int group, bool flag=false);
	///Quick code that adds in a default flag radius widget pair at the specific position, and returns the width.
	int addDefaultFlagRadiusWidget(int type, int x, int y, int group);
	///Activates the given group number for default assignment widgets
	void activateDefaultAssignedGroupNumber(int group);
	///Returns the default unit assignment text
	std::string getDefaultUnitAssignmentText(int type, int level, bool flag);
	///Sets the texts for all default unit assignment widgets
	void setLanguageTextsForDefaultAssignmentWidgets();
	
	
	virtual void onGroupActivated(int group_n);
	
	///Update shortcut_list, if n is not -1, just update that specific entry
	void updateShortcutList(int n=-1);
	///Update the action_list
	void updateActionList();
	///Updates the boxes from the current shortcut selection
	void updateShortcutInfoFromSelection();
	///Updates the KeyboardManager from the shortcut info
	void updateKeyboardManagerFromShortcutInfo();
	///Tells the KeyboardManager to load from the defaults
	void loadDefaultKeyboardShortcuts();
	///Adds a shortcut to the current keyboard manager
	void addNewShortcut();
	///Removes a shortcut from current keyboard manager
	void removeShortcut();

	SettingsScreen();
	virtual ~SettingsScreen() { }
	void onAction(Widget *source, Action action, int par1, int par2);
	static int menu(void);
};

#endif
