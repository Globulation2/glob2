// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Coordination layer for the settings screen: ctor that orchestrates the
// per-tab build helpers, onAction event-kind dispatcher and its handlers,
// the language re-translation pass, and the GfxContext / audio-mute
// visibility plumbing.
//
// Per-tab construction lives in:
//   - SettingsScreenGeneral.cpp   ("General Settings" tab widgets)
//   - SettingsScreenBuildings.cpp ("Building Defaults" tab)
//   - SettingsScreenKeyboard.cpp  ("Keyboard Shortcuts" tab)

#include "SettingsScreen.h"
#include <GUIStyle.h>
#include "GlobalContainer.h"
#include <assert.h>
#include <sstream>
#include <GUIText.h>
#include <GUITextInput.h>
#include <GUIList.h>
#include <GUIButton.h>
#include <GUISelector.h>
#include <FormatableString.h>
#include <Toolkit.h>
#include <StringTable.h>
#include "SoundMixer.h"
#include <ostream>
#include <string>

SettingsScreen::SettingsScreen()
 : Glob2TabScreen(false, true), unitRatioGroupNumbers(), flagRadii(), flagRadiusTexts(), flagRadiusGroupNumbers(), mapeditKeyboardManager(MapEditShortcuts), guiKeyboardManager(GameGUIShortcuts)
{
	old_settings=globalContainer->settings;

	generalGroup = addGroup(Toolkit::getStringTable()->getString("[general settings]"));
	unitGroup = addGroup(Toolkit::getStringTable()->getString("[building settings]"));
	keyboardGroup = addGroup(Toolkit::getStringTable()->getString("[keyboard settings]"));

	buildOkCancelButtons();
	buildLanguageWidgets();
	buildDisplayWidgets();
	buildGraphicsToggles();
	buildUsernameWidgets();
	buildAudioWidgets();

	buildBuildingDefaultsTab();
	buildKeyboardShortcutsTab();

	currentMode = GameGUIShortcuts;
	activateGroup(generalGroup);

	gfxAltered = false;
}


void SettingsScreen::setFullscreen()
{
    if(fullscreen->getState()){
        globalContainer->settings.screenFlags |= GraphicContext::FULLSCREEN;
        globalContainer->settings.screenFlags &= ~(GraphicContext::RESIZABLE);
    }else{
        globalContainer->settings.screenFlags &= ~(GraphicContext::FULLSCREEN);
        globalContainer->settings.screenFlags |= GraphicContext::RESIZABLE;
    }
    modeList->setVisible(fullscreen->getState());
    updateGfxCtx();
}

void SettingsScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	TabScreen::onAction(source, action, par1, par2);
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		handleButtonAction(par1);
	else if (action==NUMBER_ELEMENT_SELECTED)
		flushDefaultsToSettings();
	else if (action==LIST_ELEMENT_SELECTED)
		handleListSelected(source, par1);
	else if (action==VALUE_CHANGED)
		handleValueChanged(source);
	else if (action==BUTTON_STATE_CHANGED)
		handleButtonStateChanged(source);
	else if (action==KEY_CHANGED)
		updateKeyboardManagerFromShortcutInfo();
}


void SettingsScreen::handleButtonAction(int par1)
{
	if (par1==OK)
	{
		globalContainer->settings.setUsername(userName->getText());
		globalContainer->settings.language = Toolkit::getStringTable()->getStringInLang("[language-code]", Toolkit::getStringTable()->getLang());
		globalContainer->settings.save();
		mapeditKeyboardManager.saveKeyboardLayout();
		guiKeyboardManager.saveKeyboardLayout();
		endExecute(par1);
	}
	else if (par1==CANCEL)
	{
		globalContainer->settings=old_settings;
		if (gfxAltered)
			updateGfxCtx();

		Toolkit::getStringTable()->setLang(Toolkit::getStringTable()->getLangCode(globalContainer->settings.language));

		///Send the old volume to the mixer
		globalContainer->mix->setVolume(globalContainer->settings.musicVolume, globalContainer->settings.voiceVolume, globalContainer->settings.mute);

		endExecute(par1);
	}
	else if (par1==RESTOREDEFAULTSHORTCUTS)
	{
		loadDefaultKeyboardShortcuts();
	}
	else if(par1==GAMESHORTCUTS)
	{
		currentMode = GameGUIShortcuts;
		updateShortcutList();
		if(shortcut_list->getCount() == 0)
			shortcut_list->setSelectionIndex(-1);
		else
			shortcut_list->setSelectionIndex(0);
		updateActionList();
		updateShortcutInfoFromSelection();
	}
	else if(par1==EDITORSHORTCUTS)
	{
		currentMode = MapEditShortcuts;
		updateShortcutList();
		if(shortcut_list->getCount() == 0)
			shortcut_list->setSelectionIndex(-1);
		else
			shortcut_list->setSelectionIndex(0);
		updateActionList();
		updateShortcutInfoFromSelection();
	}
	else if(par1==PRESSEDSELECTOR)
	{
	}
	else if(par1==ADDSHORTCUT)
	{
		addNewShortcut();
	}
	else if(par1==REMOVESHORTCUT)
	{
		removeShortcut();
	}
	else if(par1==BUILDINGSETTINGS)
	{
		activateDefaultAssignedGroupNumber(kBuildingGroupCompleted);
	}
	else if(par1==CONSTRUCTIONSITES)
	{
		activateDefaultAssignedGroupNumber(kBuildingGroupNewConstruction);
	}
	else if(par1==UPGRADES)
	{
		activateDefaultAssignedGroupNumber(kBuildingGroupUpgrades);
	}
	else if(par1==FLAGSETTINGS)
	{
		activateDefaultAssignedGroupNumber(kBuildingGroupFlags);
	}
}


void SettingsScreen::handleListSelected(Widget* source, int par1)
{
	if (source==languageList)
	{
		Toolkit::getStringTable()->setLang(par1);
		retranslateUiStrings();
	}
	else if (source==modeList)
	{
		// Windowed dimensions follow the OS window; presets select fullscreen's
		// logical rendering resolution only.
		if (!fullscreen->getState()) return;
		int w, h;
		if (sscanf(modeList->getText(par1).c_str(), "%dx%d", &w, &h) != 2) return;
		globalContainer->settings.screenWidth=w;
		globalContainer->settings.screenHeight=h;

	    setFullscreen();
	}
	else if (source == shortcut_list)
	{
		updateShortcutInfoFromSelection();
	}
	else if(source == action_list)
	{
		updateKeyboardManagerFromShortcutInfo();
	}
}


void SettingsScreen::handleValueChanged(Widget* source)
{
	if(source==gameSpeed)
	{
		globalContainer->settings.gameSpeed=gameSpeed->getValue();
		updateGameSpeedText();
		return;
	}
	globalContainer->settings.musicVolume = musicVol->getValue();
	globalContainer->settings.voiceVolume = voiceVol->getValue();
	globalContainer->mix->setVolume(globalContainer->settings.musicVolume, globalContainer->settings.voiceVolume, globalContainer->settings.mute);
}

void SettingsScreen::updateGameSpeedText(void)
{
	gameSpeed->setTooltip(Toolkit::getStringTable()->getString("[game speed help]"), "standard");
	gameSpeedText->setText(FormattableString("%0: %1")
		.arg(Toolkit::getStringTable()->getString("[game speed]"))
		.arg(globalContainer->settings.getGameSpeedText()));
}


void SettingsScreen::handleButtonStateChanged(Widget* source)
{
	if (source==highres)
	{
		globalContainer->settings.highResolutionArtwork=highres->getState();
	}
	else if (source==rememberUnitButton)
	{
		globalContainer->settings.rememberUnit=rememberUnitButton->getState();
	}
	else if (source==scrollwheel)
	{
		globalContainer->settings.scrollWheelEnabled=scrollwheel->getState();
		scrollWheelEnabled=scrollwheel->getState();
	}
	else if (source==lowquality)
	{
		globalContainer->settings.optionFlags=lowquality->getState() ? GlobalContainer::OPTION_LOW_SPEED_GFX : 0;
	}
	else if (source==fullscreen)
	{
	    setFullscreen();
	}
	else if (source==usegpu)
	{
		if (usegpu->getState())
		{
			globalContainer->settings.screenFlags |= GraphicContext::USEGPU;
		}
		else
		{
			globalContainer->settings.screenFlags &= ~(GraphicContext::USEGPU);
		}
		updateGfxCtx();
	}
	else if (source==customcur)
	{
		if (customcur->getState())
		{
			globalContainer->settings.screenFlags |= GraphicContext::CUSTOMCURSOR;
		}
		else
		{
			globalContainer->settings.screenFlags &= ~(GraphicContext::CUSTOMCURSOR);
		}
		updateGfxCtx();
	}
	else if (source==audioMute)
	{
		globalContainer->settings.mute = audioMute->getState();
		globalContainer->mix->setVolume(globalContainer->settings.musicVolume, globalContainer->settings.voiceVolume, globalContainer->settings.mute);
		setVisibilityFromAudioSettings();
	}
	else if (source==key_2_active)
	{
		if(key_2_active->getState() == true)
		{
			select_key_2->setKey(KeyPress());
			select_key_2->visible=true;
		}
		else
		{
			select_key_2->visible=false;
		}
		updateKeyboardManagerFromShortcutInfo();
	}
}


void SettingsScreen::retranslateUiStrings()
{
	ok->setText(Toolkit::getStringTable()->getString("[ok]"));
	cancel->setText(Toolkit::getStringTable()->getString("[Cancel]"));

	modifyTitle(generalGroup, Toolkit::getStringTable()->getString("[general settings]"));
	modifyTitle(unitGroup, Toolkit::getStringTable()->getString("[building settings]"));
	modifyTitle(keyboardGroup, Toolkit::getStringTable()->getString("[keyboard settings]"));

	language->setText(Toolkit::getStringTable()->getString("[language-tr]"));
	display->setText(Toolkit::getStringTable()->getString("[display]"));
	usernameText->setText(Toolkit::getStringTable()->getString("[username]"));
	audio->setText(Toolkit::getStringTable()->getString("[audio]"));

	fullscreenText->setText(Toolkit::getStringTable()->getString("[fullscreen]"));
	usegpuText->setText(Toolkit::getStringTable()->getString("[OpenGL]"));
	lowqualityText->setText(Toolkit::getStringTable()->getString("[lowquality]"));
	customcurText->setText(Toolkit::getStringTable()->getString("[customcur]"));


	rememberUnitText->setText(Toolkit::getStringTable()->getString("[remember unit]"));
	scrollwheelText->setText(Toolkit::getStringTable()->getString("[scroll wheel enabled]"));
	updateGameSpeedText();

	musicVolText->setText(Toolkit::getStringTable()->getString("[Music volume]"));
	audioMuteText->setText(Toolkit::getStringTable()->getString("[mute]"));

	rebootWarning->setText(Toolkit::getStringTable()->getString("[Warning, you need to reboot the game for changes to take effect]"));

	unitSettingsExplanation->setText(Toolkit::getStringTable()->getString("[unit settings explanation]"));
	buildings->setText(Toolkit::getStringTable()->getString("[Building Defaults]"));
	flags->setText(Toolkit::getStringTable()->getString("[Flag Defaults]"));
	constructionsites->setText(Toolkit::getStringTable()->getString("[Construction Site Defaults]"));
	upgrades->setText(Toolkit::getStringTable()->getString("[Upgrade Defaults]"));
	setLanguageTextsForDefaultAssignmentWidgets();

	game_shortcuts->setText(Toolkit::getStringTable()->getString("[game shortcuts]"));
	editor_shortcuts->setText(Toolkit::getStringTable()->getString("[editor shortcuts]"));
	restore_default_shortcuts->setText(Toolkit::getStringTable()->getString("[restore default shortcuts]"));
	add_shortcut->setText(Toolkit::getStringTable()->getString("[add shortcut]"));
	remove_shortcut->setText(Toolkit::getStringTable()->getString("[remove shortcut]"));

	pressedUnpressedSelector->clearTexts();
	pressedUnpressedSelector->addText(Toolkit::getStringTable()->getString("[on press]"));
	pressedUnpressedSelector->addText(Toolkit::getStringTable()->getString("[on unpress]"));
}


void SettingsScreen::setVisibilityFromGraphicType(void)
{
	// a change either into or out of the GL renderer only takes effect on restart
	rebootWarning->visible = (globalContainer->settings.screenFlags & GraphicContext::USEGPU)
		|| (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU);
}

void SettingsScreen::setVisibilityFromAudioSettings(void)
{
	musicVol->visible = !globalContainer->settings.mute;
	musicVolText->visible = !globalContainer->settings.mute;
	voiceVol->visible = !globalContainer->settings.mute;
	voiceVolText->visible = !globalContainer->settings.mute;
}

void SettingsScreen::updateGfxCtx(void)
{
	// Only reconfigure live while the renderer stays software: a window created for
	// OpenGL cannot serve SDL_GetWindowSurface, so switching away from GL in place
	// leaves the old context's contents on screen until the game is restarted.
	if (((globalContainer->settings.screenFlags & GraphicContext::USEGPU) == 0)
		&& ((globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU) == 0))
		globalContainer->gfx->setRes(globalContainer->settings.screenWidth, globalContainer->settings.screenHeight, globalContainer->settings.screenFlags);
	setVisibilityFromGraphicType();
	actDisplay->setText(actDisplayModeToString().c_str());
	gfxAltered = true;
}

std::string SettingsScreen::actDisplayModeToString(void)
{
	std::ostringstream oss;
	oss << globalContainer->gfx->getW() << "x" << globalContainer->gfx->getH();
	if (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
		oss << " GL";
	else
		oss << " SDL";
	return oss.str();
}

void SettingsScreen::onSDLEvent(SDL_Event *event)
{
	Glob2TabScreen::onSDLEvent(event);
	// The event pump may coalesce resizing with a later expose/focus event.
	if (event->type == SDL_WINDOWEVENT)
		actDisplay->setText(actDisplayModeToString());
}


void SettingsScreen::onGroupActivated(int group_n)
{
	if(group_n == generalGroup)
	{
		setVisibilityFromAudioSettings();
		modeList->setVisible(fullscreen->getState());
	}
	else if(group_n == unitGroup)
	{
		activateDefaultAssignedGroupNumber(kBuildingGroupCompleted);
	}
	else if(group_n == keyboardGroup)
	{
		currentMode = GameGUIShortcuts;
		updateShortcutList();
		if(shortcut_list->getCount() == 0)
			shortcut_list->setSelectionIndex(-1);
		else
			shortcut_list->setSelectionIndex(0);
		updateActionList();
		updateShortcutInfoFromSelection();
	}
}
