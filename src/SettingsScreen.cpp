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

#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include <assert.h>
#include <sstream>
#include <GUIText.h>
#include <GUITextInput.h>
#include <GUIList.h>
#include <GUIButton.h>
#include <GUISelector.h>
#include <GUINumber.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>
#include "SoundMixer.h"
#include <ostream>
#include <algorithm>
#include "boost/lexical_cast.hpp"
#include "GameGUIKeyActions.h"
#include "MapEditKeyActions.h"

SettingsScreen::SettingsScreen()
 : mapeditKeyboardManager(MapEditShortcuts), guiKeyboardManager(GameGUIShortcuts)
{
	old_settings=globalContainer->settings;
	
	generalGroup = addGroup(Toolkit::getStringTable()->getString("[general settings]"));
	unitGroup = addGroup(Toolkit::getStringTable()->getString("[unit settings]"));
	keyboardGroup = addGroup(Toolkit::getStringTable()->getString("[keyboard settings]"));

	// Screen entry/quit part
	ok=new TextButton( 230, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[ok]"), OK);
	addWidget(ok);
	cancel=new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL);
	addWidget(cancel);

	//following are all general settings
	// language part
	language=new Text(20, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[language-tr]"));
	addWidgetToGroup(language, generalGroup);
	languageList=new List(20, 90, 180, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	for (int i=0; i<Toolkit::getStringTable()->getNumberOfLanguage(); i++)
	{
		if(!Toolkit::getStringTable()->isLangComplete(i))
			languageList->addText(Toolkit::getStringTable()->getStringInLang("[language incomplete]", i));
		else
			languageList->addText(Toolkit::getStringTable()->getStringInLang("[language]", i));
	}
	addWidgetToGroup(languageList, generalGroup);

	// graphics part
	display=new Text(230, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[display]"));
	addWidgetToGroup(display, generalGroup);
	actDisplay = new Text(440, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", actDisplayModeToString().c_str());
	addWidgetToGroup(actDisplay, generalGroup);
	modeList=new List(440, 90, 180, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	globalContainer->gfx->beginVideoModeListing();
	int w, h;
	while(globalContainer->gfx->getNextVideoMode(&w, &h))
	{
		std::ostringstream ost;
		ost << w << "x" << h;
		if (!modeList->isText(ost.str().c_str()))
			modeList->addText(ost.str().c_str());
	}
	addWidgetToGroup(modeList, generalGroup);
	
	fullscreen=new OnOffButton(230, 90, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.screenFlags & GraphicContext::FULLSCREEN, FULLSCREEN);
	addWidgetToGroup(fullscreen, generalGroup);
	fullscreenText=new Text(260, 90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[fullscreen]"), 180);
	addWidgetToGroup(fullscreenText, generalGroup);
	
	#ifdef HAVE_OPENGL
	#endif
	usegpu=new OnOffButton(230, 90 + 30, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.screenFlags & GraphicContext::USEGPU, USEGL);
	addWidgetToGroup(usegpu, generalGroup);
	usegpuText=new Text(260, 90 + 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "OpenGL", 180);
	addWidgetToGroup(usegpuText, generalGroup);
	
	lowquality=new OnOffButton(230, 90 + 60, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX, LOWQUALITY);
	addWidgetToGroup(lowquality, generalGroup);
	lowqualityText=new Text(260, 90 + 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[lowquality]"), 180);
	addWidgetToGroup(lowqualityText, generalGroup);

	customcur=new OnOffButton(230, 90 + 90, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.screenFlags & GraphicContext::CUSTOMCURSOR, CUSTOMCUR);
	addWidgetToGroup(customcur, generalGroup);
	customcurText=new Text(260, 90 + 90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[customcur]"), 180);
	addWidgetToGroup(customcurText, generalGroup);
	rememberUnitButton=new OnOffButton(230, 90 + 120, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.rememberUnit, REMEMBERUNIT);
	addWidgetToGroup(rememberUnitButton, generalGroup);
	rememberUnitText=new Text(260, 90 + 120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[remember unit]"), 180);
	addWidgetToGroup(rememberUnitText, generalGroup);

	scrollwheel=new OnOffButton(230, 90 + 150, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.scrollWheelEnabled, SCROLLWHEEL);
	addWidgetToGroup(scrollwheel, generalGroup);
	scrollwheelText=new Text(260, 90 + 150, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[scroll wheel enabled]"), 180);
	addWidgetToGroup(scrollwheelText, generalGroup);

	
	rebootWarning=new Text(0, 300, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Warning, you need to reboot the game for changes to take effect]"));
	rebootWarning->setStyle(Font::Style(Font::STYLE_BOLD, 255, 60, 60));
	addWidget(rebootWarning);
	
	setVisibilityFromGraphicType();
	rebootWarning->visible=false;

	// Username part
	userName=new TextInput(20, 360, 180, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", globalContainer->getUsername(), true, 32);
	addWidgetToGroup(userName, generalGroup);
	usernameText=new Text(20, 330, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[username]"));
	addWidgetToGroup(usernameText, generalGroup);

	// Audio part
	audio=new Text(230, 330, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[audio]"), 300);
	addWidgetToGroup(audio, generalGroup);
	audioMute=new OnOffButton(230, 365, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.mute, MUTE);
	addWidgetToGroup(audioMute, generalGroup);
	audioMuteText=new Text(260, 365, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[mute]"), 200);
	addWidgetToGroup(audioMuteText, generalGroup);
	musicVol=new Selector(320, 350, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 180, globalContainer->settings.musicVolume, 256, true);
	addWidgetToGroup(musicVol, generalGroup);
	voiceVol=new Selector(320, 385, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 180, globalContainer->settings.voiceVolume, 256, true);
	addWidgetToGroup(voiceVol, generalGroup);
	musicVolText=new Text(320, 330, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Music volume]"), 300);
	addWidgetToGroup(musicVolText, generalGroup);
	voiceVolText=new Text(320, 365, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Voice volume]"), 300);
	addWidgetToGroup(voiceVolText, generalGroup);
	setVisibilityFromAudioSettings();
	
	
	int first_group_row=0;
	int first_group_current_column_x=20;
	int first_group_widest_element=0;
	
	int second_group_row=1;
	int second_group_first_row_x=170;
	int second_group_current_column_x=170;
	int second_group_widest_element=0;
	
	for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
	{
		for(int l=0; l<6; ++l)
		{
			unitRatios[t][l]=NULL;
			unitRatioTexts[t][l]=NULL;
		}
	}
	
	///First group are completed buildings, Inn, Swarm and Defence tower
	for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
	{
		if(t==IntBuildingType::EXPLORATION_FLAG || t==IntBuildingType::WAR_FLAG || t==IntBuildingType::CLEARING_FLAG)
			continue;
		std::string name=IntBuildingType::typeFromShortNumber(t);
		for(int l=0; l<3; ++l)
		{
			if(globalContainer->buildingsTypes.getByType(name, l, false) != NULL && globalContainer->settings.defaultUnitsAssigned[t][l*2+1]>0)
			{
				int size = addDefaultUnitAssignmentWidget(t, l*2+1, first_group_current_column_x, 100 + 40*first_group_row, 1);
				first_group_widest_element = std::max(first_group_widest_element, size);
				
				first_group_row += 1;
				if(first_group_row == 8)
				{
					first_group_row = 0;
					first_group_current_column_x += first_group_widest_element;
					first_group_widest_element = 0;
				}
			}
		}	
	}
	
	///Second group, at the top is Swarm, Wall, Market, the rest follow horizontally
	for(int l=0; l<3; ++l)
	{
		for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
		{
			std::string name=IntBuildingType::typeFromShortNumber(t);
			//Even numbers represent under-construction, whereas odd numbers represent completed buildings		
			if(globalContainer->buildingsTypes.getByType(name, l, true) != NULL && globalContainer->settings.defaultUnitsAssigned[t][l*2]>0)
			{
				int this_row = second_group_row;
				int this_x = second_group_current_column_x;
				if(t == IntBuildingType::SWARM_BUILDING || t == IntBuildingType::STONE_WALL || t == IntBuildingType::MARKET_BUILDING)
				{
					this_row = 0;
					this_x = second_group_first_row_x;
					second_group_first_row_x += 180;
				}
				else
				{
					second_group_row += 1;
				}
				
				int size = addDefaultUnitAssignmentWidget(t, l*2, this_x, 100 + 40*this_row, 1);
				second_group_widest_element = std::max(second_group_widest_element, size);
				
				if(second_group_row == 8)
				{
					second_group_row = 1;
					second_group_current_column_x += 180;
					second_group_widest_element = 0;
				}
			}
		}
	}
	
	first_group_row=0;
	first_group_current_column_x=20;
	first_group_widest_element=0;
	///On the second screen, the only group is first
	for(int t=IntBuildingType::EXPLORATION_FLAG; t<=IntBuildingType::CLEARING_FLAG; ++t)
	{
		int size = addDefaultUnitAssignmentWidget(t, 1, first_group_current_column_x, 100 + 40*first_group_row, 2, true);
		first_group_widest_element = std::max(first_group_widest_element, size);
		
		first_group_row += 1;
		if(first_group_row == 8)
		{
			first_group_row = 0;
			first_group_current_column_x += first_group_widest_element;
			first_group_widest_element = 0;
		}
	}
	
	flags = new TextButton( 10, 60, 120, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Building Settings]"), BUILDINGSETTINGS);
	buildings = new TextButton( 140, 60, 120, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Flag Settings]"), FLAGSETTINGS);
	
	unitSettingsExplanation = new Text( 270, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[unit settings explanation]"));
	unitSettingsHeading1 = new Text( 160, 80, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[construction and upgrades]"));
	unitSettingsHeading2 = new Text( 10, 80, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[constructed buildings]"));

	addWidgetToGroup(unitSettingsExplanation, unitGroup);
	addWidgetToGroup(unitSettingsHeading1, unitGroup);
	addWidgetToGroup(unitSettingsHeading2, unitGroup);
	addWidgetToGroup(buildings, unitGroup);
	addWidgetToGroup(flags, unitGroup);

	//shortcuts part
	game_shortcuts=new TextButton( 100, 60, 120, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[game shortcuts]"), GAMESHORTCUTS);

	editor_shortcuts=new TextButton( 230, 60, 120, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[editor shortcuts]"), EDITORSHORTCUTS);

	shortcut_list = new List(20, 110, 325, 160, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	action_list = new List(365, 110 , 265, 190, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	select_key_1 = new KeySelector(20, 275, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", 100, 25);
	key_2_active = new OnOffButton(125, 275, 25, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, false, SECONDKEY);
	select_key_2 = new KeySelector(155, 275, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", 100, 25);
	pressedUnpressedSelector = new MultiTextButton(260, 275, 80, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", PRESSEDSELECTOR);
	add_shortcut = new TextButton(20, 305, 158, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[add shortcut]"), ADDSHORTCUT);
	remove_shortcut = new TextButton(188, 305, 157, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[remove shortcut]"), REMOVESHORTCUT);
	restore_default_shortcuts = new TextButton(365, 305, 265, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[restore default shortcuts]"), RESTOREDEFAULTSHORTCUTS);

	pressedUnpressedSelector->clearTexts();
	pressedUnpressedSelector->addText(Toolkit::getStringTable()->getString("[on press]"));
	pressedUnpressedSelector->addText(Toolkit::getStringTable()->getString("[on unpress]"));
		
	addWidgetToGroup(game_shortcuts, keyboardGroup);
	addWidgetToGroup(editor_shortcuts, keyboardGroup);
	addWidgetToGroup(shortcut_list, keyboardGroup);
	addWidgetToGroup(action_list, keyboardGroup);
	addWidgetToGroup(select_key_1, keyboardGroup);
	addWidgetToGroup(key_2_active, keyboardGroup);
	addWidgetToGroup(select_key_2, keyboardGroup);
	addWidgetToGroup(pressedUnpressedSelector, keyboardGroup);
	addWidgetToGroup(add_shortcut, keyboardGroup);
	addWidgetToGroup(remove_shortcut, keyboardGroup);
	addWidgetToGroup(restore_default_shortcuts, keyboardGroup);

	currentMode = GameGUIShortcuts;

	activateGroup(generalGroup);

	gfxAltered = false;
}


void SettingsScreen::addNumbersFor(int low, int high, Number* widget)
{
	for(int i=low; i<=high; ++i)
	{
		widget->add(i);
	}
}


void SettingsScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	TabScreen::onAction(source, action, par1, par2);
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==OK)
		{
			globalContainer->setUserName(userName->getText());
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
			activateDefaultAssignedGroupNumber(1);
			unitSettingsHeading1->visible=true;
			unitSettingsHeading2->visible=true;
		}
		else if(par1==FLAGSETTINGS)
		{
			activateDefaultAssignedGroupNumber(2);
			unitSettingsHeading1->visible=false;
			unitSettingsHeading2->visible=false;
		}
	}
	else if (action==NUMBER_ELEMENT_SELECTED)
	{
		for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
		{
			for(int l=0; l<6; ++l)
			{
				if(unitRatios[t][l])
				{
					if(unitRatios[t][l]->getNth() == 0)
						unitRatios[t][l]->setNth(1);
					globalContainer->settings.defaultUnitsAssigned[t][l]=unitRatios[t][l]->getNth();
				}
			}
		}
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		if (source==languageList)
		{
			Toolkit::getStringTable()->setLang(par1);
			ok->setText(Toolkit::getStringTable()->getString("[ok]"));
			cancel->setText(Toolkit::getStringTable()->getString("[Cancel]"));

//;			title->setText(Toolkit::getStringTable()->getString("[settings]"));
			language->setText(Toolkit::getStringTable()->getString("[language-tr]"));
			display->setText(Toolkit::getStringTable()->getString("[display]"));
			usernameText->setText(Toolkit::getStringTable()->getString("[username]"));
			audio->setText(Toolkit::getStringTable()->getString("[audio]"));

			generalsettings->setText(Toolkit::getStringTable()->getString("[general settings]"));
			unitsettings->setText(Toolkit::getStringTable()->getString("[unit settings]"));
			keyboardsettings->setText(Toolkit::getStringTable()->getString("[keyboard settings]"));

			fullscreenText->setText(Toolkit::getStringTable()->getString("[fullscreen]"));
			//usegpuText->setText(Toolkit::getStringTable()->getString("[opengl]"));
			lowqualityText->setText(Toolkit::getStringTable()->getString("[lowquality]"));
			customcurText->setText(Toolkit::getStringTable()->getString("[customcur]"));


			rememberUnitText->setText(Toolkit::getStringTable()->getString("[remember unit]"));
			scrollwheelText->setText(Toolkit::getStringTable()->getString("[scroll wheel enabled]"));

			musicVolText->setText(Toolkit::getStringTable()->getString("[Music volume]"));
			audioMuteText->setText(Toolkit::getStringTable()->getString("[mute]"));
			
			rebootWarning->setText(Toolkit::getStringTable()->getString("[Warning, you need to reboot the game for changes to take effect]"));
			
			unitSettingsExplanation->setText(Toolkit::getStringTable()->getString("[unit settings explanation]"));
			unitSettingsHeading1->setText(Toolkit::getStringTable()->getString("[construction and upgrades]"));
			unitSettingsHeading2->setText(Toolkit::getStringTable()->getString("[constructed buildings]"));
			buildings->setText(Toolkit::getStringTable()->getString("[Building Settings]"));
			flags->setText(Toolkit::getStringTable()->getString("[Flag Settings]"));
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
		else if (source==modeList)
		{
			int w, h;
			sscanf(modeList->getText(par1).c_str(), "%dx%d", &w, &h);
			globalContainer->settings.screenWidth=w;
			globalContainer->settings.screenHeight=h;
			updateGfxCtx();
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
	else if (action==VALUE_CHANGED)
	{
		globalContainer->settings.musicVolume = musicVol->getValue();
		globalContainer->settings.voiceVolume = voiceVol->getValue();
		globalContainer->mix->setVolume(globalContainer->settings.musicVolume, globalContainer->settings.voiceVolume, globalContainer->settings.mute);
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		if (source==rememberUnitButton)
		{
			globalContainer->settings.rememberUnit=rememberUnitButton->getState();
		}
		else if (source==scrollwheel)
		{
			globalContainer->settings.scrollWheelEnabled=scrollwheel->getState();
		}
		else if (source==lowquality)
		{
			globalContainer->settings.optionFlags=lowquality->getState() ? GlobalContainer::OPTION_LOW_SPEED_GFX : 0;
		}
		else if (source==fullscreen)
		{
			if (fullscreen->getState())
			{
				globalContainer->settings.screenFlags |= GraphicContext::FULLSCREEN;
				globalContainer->settings.screenFlags &= ~(GraphicContext::RESIZABLE);
			}
			else
			{
				globalContainer->settings.screenFlags &= ~(GraphicContext::FULLSCREEN);
				globalContainer->settings.screenFlags |= GraphicContext::RESIZABLE;
			}
			updateGfxCtx();
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
	else if (action==KEY_CHANGED)
	{
		updateKeyboardManagerFromShortcutInfo();
	}
}


void SettingsScreen::setVisibilityFromGraphicType(void)
{
	rebootWarning->visible = globalContainer->settings.screenFlags & GraphicContext::USEGPU;
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
	if ((globalContainer->settings.screenFlags & GraphicContext::USEGPU) == 0)
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



int SettingsScreen::addDefaultUnitAssignmentWidget(int type, int level, int x, int y, int group, bool flag)
{
	unitRatios[type][level] = new Number(x, y+20, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	addNumbersFor(0, 20, unitRatios[type][level]);
	unitRatios[type][level]->setNth(globalContainer->settings.defaultUnitsAssigned[type][level]);
	unitRatios[type][level]->visible=false;
	addWidgetToGroup(unitRatios[type][level], unitGroup);

	std::string text=getDefaultUnitAssignmentText(type, level, flag);
	unitRatioTexts[type][level]=new Text(x, y, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", text);
	
	addWidgetToGroup(unitRatioTexts[type][level], unitGroup);
	unitRatioTexts[type][level]->visible=false;
	unitRatioGroupNumbers[type][level] = group;

	return std::max(unitRatioTexts[type][level]->getWidth(), unitRatios[type][level]->getWidth());
}



std::string SettingsScreen::getDefaultUnitAssignmentText(int type, int level, bool flag)
{
	std::string name=IntBuildingType::typeFromShortNumber(type);

	std::string keyname="[";
	if(flag)
	{
		keyname+=name + "]";
	}
	else
	{
		if((level+1)%2)
			keyname+="build ";
		keyname+=name + " level " + boost::lexical_cast<std::string>(level/2) + "]";
	}
	return Toolkit::getStringTable()->getString(keyname.c_str());
}



void SettingsScreen::setLanguageTextsForDefaultAssignmentWidgets()
{
	for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
	{
		bool flag = false;
		if(t == IntBuildingType::EXPLORATION_FLAG || t == IntBuildingType::WAR_FLAG || t == IntBuildingType::CLEARING_FLAG)
			flag = true;
		for(int l=0; l<6; ++l)
		{
			if(unitRatioTexts[t][l])
			{
				unitRatioTexts[t][l]->setText(getDefaultUnitAssignmentText(t, l, flag));
			}
		}
	}
}



void SettingsScreen::activateDefaultAssignedGroupNumber(int group)
{
	for(int i=0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		for(int j=0; j<6; ++j)
		{
			if(unitRatioGroupNumbers[i][j] == group)
			{
				if(unitRatios[i][j])
					unitRatios[i][j]->visible=true;
				if(unitRatioTexts[i][j])
					unitRatioTexts[i][j]->visible=true;
			}
			else
			{
				if(unitRatios[i][j])
					unitRatios[i][j]->visible=false;
				if(unitRatioTexts[i][j])
					unitRatioTexts[i][j]->visible=false;
			}
		}
	}
}


void SettingsScreen::onGroupActivated(int group_n)
{
	if(group_n == unitGroup)
	{
		activateDefaultAssignedGroupNumber(1);
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

void SettingsScreen::updateShortcutList(int an)
{
	KeyboardManager* m = NULL;
	if(currentMode == GameGUIShortcuts)
		m = &guiKeyboardManager;
	else if(currentMode == MapEditShortcuts)
		m = &mapeditKeyboardManager;
	
	const std::list<KeyboardShortcut>& shortcuts = m->getKeyboardShortcuts();
	size_t n = 0;
	for(std::list<KeyboardShortcut>::const_iterator i = shortcuts.begin(); i!=shortcuts.end(); ++i)
	{
		if(an==-1 || int(n) == an)
		{
			std::string name = i->formatTranslated(currentMode);
			if(n >= shortcut_list->getCount())
				shortcut_list->addText(name);
			else if(shortcut_list->getText(n) != name)
				shortcut_list->setText(n, name);
		}
		n += 1;
	}
	//Remove entries that are off the end
	while(n < shortcut_list->getCount())
		shortcut_list->removeText(n);
}



void SettingsScreen::updateActionList()
{
	action_list->clear();
	if(shortcut_list->getSelectionIndex() != -1)
	{
		if(currentMode == GameGUIShortcuts)
		{
			for(int i=GameGUIKeyActions::DoNothing; i<GameGUIKeyActions::ActionSize; ++i)
			{
				std::string key = "[" + GameGUIKeyActions::getName(i) + "]";
				action_list->addText(Toolkit::getStringTable()->getString(key.c_str()));
			}
		}
		else if(currentMode == MapEditShortcuts)
		{
			for(int i=MapEditKeyActions::DoNothing; i<MapEditKeyActions::ActionSize; ++i)
			{
				std::string key = "[" + MapEditKeyActions::getName(i) + "]";
				action_list->addText(Toolkit::getStringTable()->getString(key.c_str()));
			}
		}
	}
}



void SettingsScreen::updateShortcutInfoFromSelection()
{
	KeyboardManager* m = NULL;
	if(currentMode == GameGUIShortcuts)
		m = &guiKeyboardManager;
	else if(currentMode == MapEditShortcuts)
		m = &mapeditKeyboardManager;

	const std::list<KeyboardShortcut>& shortcuts = m->getKeyboardShortcuts();
	int selection_n = shortcut_list->getSelectionIndex();

	if(selection_n == -1)
	{
		select_key_1->visible=false;
		key_2_active->visible=false;
		select_key_2->visible=false;
		action_list->visible=false;
	}
	else
	{
		std::list<KeyboardShortcut>::const_iterator i = shortcuts.begin();
		std::advance(i, selection_n);
		select_key_1->setKey(i->getKeyPress(0));
		if(i->getKeyPressCount() == 1)
		{
			key_2_active->setState(false);
			select_key_2->visible=false;
		}
		else
		{
			select_key_2->setKey(i->getKeyPress(1));
			key_2_active->setState(true);
			select_key_2->visible=true;
		}

		if(i->getKeyPress(0).getPressed())
			pressedUnpressedSelector->setIndex(0);
		else
			pressedUnpressedSelector->setIndex(1);

		action_list->setSelectionIndex(i->getAction());
		action_list->centerOnItem(action_list->getSelectionIndex());
	}
}



void SettingsScreen::updateKeyboardManagerFromShortcutInfo()
{
	KeyboardManager* m = NULL;
	if(currentMode == GameGUIShortcuts)
		m = &guiKeyboardManager;
	else if(currentMode == MapEditShortcuts)
		m = &mapeditKeyboardManager;

	std::list<KeyboardShortcut>& shortcuts = m->getKeyboardShortcuts();
	int selection_n = shortcut_list->getSelectionIndex();

	if(selection_n != -1)
	{
		std::list<KeyboardShortcut>::iterator i = shortcuts.begin();
		std::advance(i, selection_n);
		KeyboardShortcut new_shortcut;
		
		KeyPress first = KeyPress(select_key_1->getKey(), (pressedUnpressedSelector->getIndex() == 0 ? true : false));
		KeyPress second = KeyPress(select_key_2->getKey(), (pressedUnpressedSelector->getIndex() == 0 ? true : false));
		
		new_shortcut.addKeyPress(first);
		if(key_2_active->getState())
			new_shortcut.addKeyPress(second);
		new_shortcut.setAction(action_list->getSelectionIndex());
		(*i) = new_shortcut;
		updateShortcutList(selection_n);
	}
}



void SettingsScreen::loadDefaultKeyboardShortcuts()
{
	KeyboardManager* m = NULL;
	if(currentMode == GameGUIShortcuts)
		m = &guiKeyboardManager;
	else if(currentMode == MapEditShortcuts)
		m = &mapeditKeyboardManager;
	m->loadDefaultShortcuts();
	updateShortcutList();
	updateShortcutInfoFromSelection();
}



void SettingsScreen::addNewShortcut()
{
	KeyboardShortcut ks;
	ks.addKeyPress(KeyPress());
	if(currentMode == GameGUIShortcuts)
	{
		ks.setAction(GameGUIKeyActions::DoNothing);
		std::list<KeyboardShortcut>& shortcuts = guiKeyboardManager.getKeyboardShortcuts();
		shortcuts.push_back(ks);
	}
	else if(currentMode == MapEditShortcuts)
	{
		ks.setAction(MapEditKeyActions::DoNothing);
		std::list<KeyboardShortcut>& shortcuts = mapeditKeyboardManager.getKeyboardShortcuts();
		shortcuts.push_back(ks);
	}
	updateShortcutList(shortcut_list->getCount());
	shortcut_list->setSelectionIndex(shortcut_list->getCount()-1);
	shortcut_list->centerOnItem(shortcut_list->getCount()-1);
	updateShortcutInfoFromSelection();
}



void SettingsScreen::removeShortcut()
{
	int selection_n = shortcut_list->getSelectionIndex();
	if(currentMode == GameGUIShortcuts)
	{
		std::list<KeyboardShortcut>& shortcuts = guiKeyboardManager.getKeyboardShortcuts();
		std::list<KeyboardShortcut>::iterator i = shortcuts.begin();
		std::advance(i, selection_n);
		shortcuts.erase(i);
	}
	else if(currentMode == MapEditShortcuts)
	{
		std::list<KeyboardShortcut>& shortcuts = mapeditKeyboardManager.getKeyboardShortcuts();
		std::list<KeyboardShortcut>::iterator i = shortcuts.begin();
		std::advance(i, selection_n);
		shortcuts.erase(i);
	}
	shortcut_list->setSelectionIndex(std::max(0, selection_n-1));
	updateShortcutList();
	updateShortcutInfoFromSelection();
}



int SettingsScreen::menu(void)
{
	return SettingsScreen().execute(globalContainer->gfx, 30);
}
