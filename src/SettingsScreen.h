// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

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
		SECONDKEY=15,
		ADDSHORTCUT=16,
		REMOVESHORTCUT=17,
		SCROLLWHEEL=18,
		BUILDINGSETTINGS=19,
		CONSTRUCTIONSITES=20,
		UPGRADES=21,
		FLAGSETTINGS=22,
		PRESSEDSELECTOR=23,
	};

	// IDs for the four sub-groups inside the "Building Defaults" tab. Stored in
	// unitRatioGroupNumbers / flagRadiusGroupNumbers and matched in
	// activateDefaultAssignedGroupNumber to control which set of widgets is
	// currently visible.
	static constexpr int kBuildingGroupCompleted = 1;
	static constexpr int kBuildingGroupNewConstruction = 2;
	static constexpr int kBuildingGroupUpgrades = 3;
	static constexpr int kBuildingGroupFlags = 4;
private:
	Settings old_settings;
	List *languageList;
	List *modeList;
	Text *modeListNote;
	TextInput *userName;
	
	TextButton *ok, *cancel;
	TextButton *buildings, *flags, *constructionsites, *upgrades;
	OnOffButton *fullscreen, *usegpu, *lowquality, *customcur, *scrollwheel;
	Selector *musicVol;
	Selector *voiceVol;
	OnOffButton *audioMute, *rememberUnitButton;
	Number* unitRatios[IntBuildingType::NB_BUILDING][6];
	Text* unitRatioTexts[IntBuildingType::NB_BUILDING][6];
	int unitRatioGroupNumbers[IntBuildingType::NB_BUILDING][6];
	Number* flagRadii[3];
	Text* flagRadiusTexts[3];
	int flagRadiusGroupNumbers[3];
	Text *language, *display, *usernameText, *audio;
	Text *fullscreenText, *usegpuText, *lowqualityText, *customcurText, *musicVolText, *audioMuteText, *voiceVolText, *rememberUnitText, *scrollwheelText;
	Text *actDisplay;
	Text *rebootWarning;

	void addNumbersFor(int low, int high, Number* widget);

	// Constructor helpers — each builds a logical chunk of widgets for the screen.
	// Split out so the construction order reads top-to-bottom without buried sub-loops.
	void buildOkCancelButtons();
	void buildLanguageWidgets();
	void buildDisplayWidgets();
	void buildGraphicsToggles();
	void buildUsernameWidgets();
	void buildAudioWidgets();
	void buildBuildingDefaultsTab();
	void buildCompletedBuildingsGroup();
	void buildNewConstructionGroup();
	void buildUpgradesGroup();
	void buildFlagsGroup();
	void buildKeyboardShortcutsTab();

	// onAction dispatch helpers — one per event kind.
	void handleButtonAction(int par1);
	void flushDefaultsToSettings();
	void handleListSelected(Widget* source, int par1);
	void handleValueChanged();
	void handleButtonStateChanged(Widget* source);
	// Re-applies the current locale to every string-bearing widget. Called after the
	// user picks a new language in the language list — every label, button, and text
	// has to be re-resolved against the new string table.
	void retranslateUiStrings();

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
	KeyboardManager mapeditKeyboardManager;
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

