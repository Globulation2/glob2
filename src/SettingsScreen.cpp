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

SettingsScreen::SettingsScreen()
{
	//following are standard choices for all screens
	//tab choices
	generalsettings=new TextButton( 10, 10, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[general settings]"), GENERALSETTINGS, 8);
	addWidget(generalsettings);

	unitsettings=new TextButton( 220, 10, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[unit settings]"), UNITSETTINGS, 9);
	addWidget(unitsettings);

	keyboardsettings=new TextButton( 430, 10, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[keyboard settings]"), KEYBOARDSETTINGS, 9);
	addWidget(keyboardsettings);

	// Screen entry/quit part
	ok=new TextButton( 440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	addWidget(ok);
	cancel=new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, NULL, -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	addWidget(cancel);

	//following are all general settings
	// language part
	language=new Text(20, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[language-tr]"));
	addWidget(language);
	languageList=new List(20, 90, 180, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	for (int i=0; i<Toolkit::getStringTable()->getNumberOfLanguage(); i++)
		languageList->addText(Toolkit::getStringTable()->getStringInLang("[language]", i));
	addWidget(languageList);

	// graphics part
	display=new Text(230, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[display]"));
	addWidget(display);
	actDisplay = new Text(440, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", actDisplayModeToString().c_str());
	addWidget(actDisplay);
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
	addWidget(modeList);
	
	fullscreen=new OnOffButton(230, 90, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.screenFlags & GraphicContext::FULLSCREEN, FULLSCREEN);
	addWidget(fullscreen);
	fullscreenText=new Text(260, 90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[fullscreen]"), 180);
	addWidget(fullscreenText);
	
	#ifdef HAVE_OPENGL
	#endif
	usegpu=new OnOffButton(230, 90 + 30, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.screenFlags & GraphicContext::USEGPU, USEGL);
	addWidget(usegpu);
	usegpuText=new Text(260, 90 + 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "OpenGL", 180);
	addWidget(usegpuText);
	
	lowquality=new OnOffButton(230, 90 + 60, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX, LOWQUALITY);
	addWidget(lowquality);
	lowqualityText=new Text(260, 90 + 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[lowquality]"), 180);
	addWidget(lowqualityText);

	customcur=new OnOffButton(230, 90 + 90, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.screenFlags & GraphicContext::CUSTOMCURSOR, CUSTOMCUR);
	addWidget(customcur);
	customcurText=new Text(260, 90 + 90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[customcur]"), 180);
	addWidget(customcurText);
	//TODO currently rememberUnitButton does nothing but is meant to eventually allow the game to remember the last number of globs assigned to a building/flag so the game will not always use the defaults
	rememberUnitButton=new OnOffButton(230, 90 + 120, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.rememberUnit, REMEMBERUNIT);
	addWidget(rememberUnitButton);
	rememberUnitText=new Text(260, 90 + 120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[remember unit]"), 180);
	addWidget(rememberUnitText);


	
	rebootWarning=new Text(0, 300, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Warning, you need to reboot the game for changes to take effect]"));
	rebootWarning->setStyle(Font::Style(Font::STYLE_BOLD, 255, 60, 60));
	addWidget(rebootWarning);
	
	setVisibilityFromGraphicType();
	rebootWarning->visible=false;

	// Username part
	userName=new TextInput(20, 360, 180, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", globalContainer->getUsername(), true, 32);
	addWidget(userName);
	usernameText=new Text(20, 330, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[username]"));
	addWidget(usernameText);

	// Audio part
	audio=new Text(230, 330, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[audio]"), 300);
	addWidget(audio);
	audioMute=new OnOffButton(230, 360, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.mute, MUTE);
	addWidget(audioMute);
	audioMuteText=new Text(260, 360, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[mute]"), 200);
	addWidget(audioMuteText);
	musicVol=new Selector(230, 420, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 180, globalContainer->settings.musicVolume, 256);
	addWidget(musicVol);
	musicVolText=new Text(230, 390, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Music volume]"), 300);
	addWidget(musicVolText);
	setVisibilityFromAudioSettings();
	
	//following are all unit settings
	warflagUnitRatio=new Number(20, 80, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	warflagUnitRatio->add(0);
	warflagUnitRatio->add(1);
	warflagUnitRatio->add(2);
	warflagUnitRatio->add(3);
	warflagUnitRatio->add(4);
	warflagUnitRatio->add(5);
	warflagUnitRatio->add(6);
	warflagUnitRatio->add(7);
	warflagUnitRatio->add(8);
	warflagUnitRatio->add(9);
	warflagUnitRatio->add(10);
	warflagUnitRatio->add(11);
	warflagUnitRatio->add(12);
	warflagUnitRatio->add(13);
	warflagUnitRatio->add(14);
	warflagUnitRatio->add(15);
	warflagUnitRatio->add(16);
	warflagUnitRatio->add(17);
	warflagUnitRatio->add(18);
	warflagUnitRatio->add(19);
	warflagUnitRatio->add(20);
	warflagUnitRatio->setNth(globalContainer->settings.warflagUnit);
	warflagUnitRatio->visible=false;
	addWidget(warflagUnitRatio);
	
	warflagUnitText=new Text(20, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[war flag]"));
	addWidget(warflagUnitText);
	warflagUnitText->visible=false;


	clearflagUnitRatio=new Number(20, 140, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	clearflagUnitRatio->add(0);
	clearflagUnitRatio->add(1);
	clearflagUnitRatio->add(2);
	clearflagUnitRatio->add(3);
	clearflagUnitRatio->add(4);
	clearflagUnitRatio->add(5);
	clearflagUnitRatio->add(6);
	clearflagUnitRatio->add(7);
	clearflagUnitRatio->add(8);
	clearflagUnitRatio->add(9);
	clearflagUnitRatio->add(10);
	clearflagUnitRatio->add(11);
	clearflagUnitRatio->add(12);
	clearflagUnitRatio->add(13);
	clearflagUnitRatio->add(14);
	clearflagUnitRatio->add(15);
	clearflagUnitRatio->add(16);
	clearflagUnitRatio->add(17);
	clearflagUnitRatio->add(18);
	clearflagUnitRatio->add(19);
	clearflagUnitRatio->add(20);
	clearflagUnitRatio->setNth(globalContainer->settings.clearflagUnit);
	clearflagUnitRatio->visible=false;
	addWidget(clearflagUnitRatio);
	
	clearflagUnitText=new Text(20, 120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[clearing flag]"));
	addWidget(clearflagUnitText);
	clearflagUnitText->visible=false;

	
	exploreflagUnitRatio=new Number(20, 200, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	exploreflagUnitRatio->add(0);
	exploreflagUnitRatio->add(1);
	exploreflagUnitRatio->add(2);
	exploreflagUnitRatio->add(3);
	exploreflagUnitRatio->add(4);
	exploreflagUnitRatio->add(5);
	exploreflagUnitRatio->add(6);
	exploreflagUnitRatio->add(7);
	exploreflagUnitRatio->add(8);
	exploreflagUnitRatio->add(9);
	exploreflagUnitRatio->add(10);
	exploreflagUnitRatio->add(11);
	exploreflagUnitRatio->add(12);
	exploreflagUnitRatio->add(13);
	exploreflagUnitRatio->add(14);
	exploreflagUnitRatio->add(15);
	exploreflagUnitRatio->add(16);
	exploreflagUnitRatio->add(17);
	exploreflagUnitRatio->add(18);
	exploreflagUnitRatio->add(19);
	exploreflagUnitRatio->add(20);
	exploreflagUnitRatio->setNth(globalContainer->settings.exploreflagUnit);
	exploreflagUnitRatio->visible=false;
	addWidget(exploreflagUnitRatio);
	
	exploreflagUnitText=new Text(20, 180, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[exploration flag]"));
	addWidget(exploreflagUnitText);
	exploreflagUnitText->visible=false;

	keyboard_shortcut_names=new List(20, 90, 325, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	keyboard_shortcut_names->visible=false;

	keyboard_shortcuts=new List(355, 90, 275, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	keyboard_shortcuts->visible=false;

	restore_default_shortcuts = new TextButton(20, 300, 610, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[restore default shortcuts]"), RESTOREDEFAULTSHORTCUTS);
	restore_default_shortcuts->visible=false;


	addWidget(keyboard_shortcut_names);
	addWidget(keyboard_shortcuts);
	addWidget(restore_default_shortcuts);

	for(std::map<std::string, std::string>::iterator i=globalContainer->settings.keyboard_shortcuts.begin(); i!=globalContainer->settings.keyboard_shortcuts.end(); ++i)
	{
		internal_names.push_back(i->first);
		std::string keyname=Toolkit::getStringTable()->getString(("["+i->first+"]").c_str());
		std::string valname=Toolkit::getStringTable()->getString((i->second=="" ? "[unassigned]" : "["+i->second+"]").c_str());
		keyboard_shortcut_names->addText(keyname + " - " + valname);
	}

	keyboard_shortcuts->addText(Toolkit::getStringTable()->getString("[unassigned]"));
	shortcut_actions.push_back("");
	keyboard_shortcuts->addText(Toolkit::getStringTable()->getString("[toggle draw unit paths]"));
	shortcut_actions.push_back("toggle draw unit paths");
	keyboard_shortcuts->addText(Toolkit::getStringTable()->getString("[destroy building]"));
	shortcut_actions.push_back("destroy building");
	keyboard_shortcuts->addText(Toolkit::getStringTable()->getString("[upgrade building]"));
	shortcut_actions.push_back("upgrade building");
	keyboard_shortcuts->addText(Toolkit::getStringTable()->getString("[repair building]"));
	shortcut_actions.push_back("repair building");
	keyboard_shortcuts->addText(Toolkit::getStringTable()->getString("[toggle draw information]"));
	shortcut_actions.push_back("toggle draw information");
	keyboard_shortcuts->addText(Toolkit::getStringTable()->getString("[toggle draw accessibility aids]"));
	shortcut_actions.push_back("toggle draw accessibility aids");
	keyboard_shortcuts->addText(Toolkit::getStringTable()->getString("[mark map]"));
	shortcut_actions.push_back("mark map");
	keyboard_shortcuts->addText(Toolkit::getStringTable()->getString("[record voice]"));
	shortcut_actions.push_back("record voice");
	keyboard_shortcuts->addText(Toolkit::getStringTable()->getString("[pause game]"));
	shortcut_actions.push_back("pause game");




	oldLanguage = Toolkit::getStringTable()->getLang();
	oldScreenW = globalContainer->settings.screenWidth;
	oldScreenH = globalContainer->settings.screenHeight;
	oldScreenFlags = globalContainer->settings.screenFlags;
	oldOptionFlags = globalContainer->settings.optionFlags;
	oldMusicVol = globalContainer->settings.musicVolume;
	oldMute = globalContainer->settings.mute;
	oldwarflagUnit = globalContainer->settings.warflagUnit;
	oldclearflagUnit = globalContainer->settings.clearflagUnit;
	oldexploreflagUnit = globalContainer->settings.exploreflagUnit;

	gfxAltered = false;
}

void SettingsScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==OK)
		{
			globalContainer->setUserName(userName->getText());

			globalContainer->settings.defaultLanguage = Toolkit::getStringTable()->getLang();
			

			globalContainer->settings.save();

			endExecute(par1);
		}
		else if (par1==CANCEL)
		{
			Toolkit::getStringTable()->setLang(oldLanguage);

			globalContainer->settings.screenWidth = oldScreenW;
			globalContainer->settings.screenHeight = oldScreenH;
			globalContainer->settings.screenFlags = oldScreenFlags;
			if (gfxAltered)
				updateGfxCtx();

			globalContainer->settings.optionFlags = oldOptionFlags;
			
			globalContainer->settings.musicVolume = oldMusicVol;
			globalContainer->settings.mute = oldMute;
			globalContainer->mix->setVolume(oldMusicVol, oldMute);
			globalContainer->settings.warflagUnit = oldwarflagUnit;
			globalContainer->settings.clearflagUnit = oldclearflagUnit;
			globalContainer->settings.exploreflagUnit = oldexploreflagUnit;

			endExecute(par1);
		}
		else if (par1==GENERALSETTINGS)	
		{
			language->visible=true;
			languageList->visible=true;
			display->visible=true;
			actDisplay->visible=true;
			modeList->visible=true;
			fullscreen->visible=true;
			fullscreenText->visible=true;
			usegpu->visible=true;
			usegpuText->visible=true;
			lowquality->visible=true;
			lowqualityText->visible=true;
			customcur->visible=true;
			customcurText->visible=true;
			userName->visible=true;
			usernameText->visible=true;
			audio->visible=true;
			audioMuteText->visible=true;
			audioMute->visible=true;
			musicVol->visible=true;
			musicVolText->visible=true;
			rememberUnitButton->visible=true;
			rememberUnitText->visible=true;

			warflagUnitRatio->visible=false;
			warflagUnitText->visible=false;
			clearflagUnitRatio->visible=false;
			clearflagUnitText->visible=false;
			exploreflagUnitRatio->visible=false;
			exploreflagUnitText->visible=false;

			keyboard_shortcut_names->visible=false;
			keyboard_shortcuts->visible=false;
			restore_default_shortcuts->visible=false;
		}

		
		else if (par1==UNITSETTINGS)
		{
			language->visible=false;
			languageList->visible=false;
			display->visible=false;
			actDisplay->visible=false;
			modeList->visible=false;
			fullscreen->visible=false;
			fullscreenText->visible=false;
			usegpu->visible=false;
			usegpuText->visible=false;
			lowquality->visible=false;
			lowqualityText->visible=false;
			customcur->visible=false;
			customcurText->visible=false;
			userName->visible=false;
			usernameText->visible=false;
			audio->visible=false;
			audioMuteText->visible=false;
			audioMute->visible=false;
			musicVol->visible=false;
			musicVolText->visible=false;
			rememberUnitButton->visible=false;
			rememberUnitText->visible=false;

			warflagUnitRatio->visible=true;
			warflagUnitText->visible=true;
			clearflagUnitRatio->visible=true;
			clearflagUnitText->visible=true;
			exploreflagUnitRatio->visible=true;
			exploreflagUnitText->visible=true;

			keyboard_shortcut_names->visible=false;
			keyboard_shortcuts->visible=false;
			restore_default_shortcuts->visible=false;
		}

		else if (par1==KEYBOARDSETTINGS)
		{
			language->visible=false;
			languageList->visible=false;
			display->visible=false;
			actDisplay->visible=false;
			modeList->visible=false;
			fullscreen->visible=false;
			fullscreenText->visible=false;
			usegpu->visible=false;
			usegpuText->visible=false;
			lowquality->visible=false;
			lowqualityText->visible=false;
			customcur->visible=false;
			customcurText->visible=false;
			userName->visible=false;
			usernameText->visible=false;
			audio->visible=false;
			audioMuteText->visible=false;
			audioMute->visible=false;
			musicVol->visible=false;
			musicVolText->visible=false;
			rememberUnitButton->visible=false;
			rememberUnitText->visible=false;
			
			warflagUnitRatio->visible=false;
			warflagUnitText->visible=false;
			clearflagUnitRatio->visible=false;
			clearflagUnitText->visible=false;
			exploreflagUnitRatio->visible=false;
			exploreflagUnitText->visible=false;

			keyboard_shortcut_names->visible=true;
			keyboard_shortcuts->visible=true;
			restore_default_shortcuts->visible=true;
		}
		else if (par1==RESTOREDEFAULTSHORTCUTS)
		{
			globalContainer->settings.restoreDefaultShortcuts();
			unsigned int pos=keyboard_shortcut_names->getSelectionIndex();
			keyboard_shortcut_names->setSelectionIndex(-1);
			for(unsigned int x=0; x<globalContainer->settings.keyboard_shortcuts.size(); ++x)
			{
				keyboard_shortcut_names->removeText(0);
			}
			for(std::map<std::string, std::string>::iterator i=globalContainer->settings.keyboard_shortcuts.begin(); i!=globalContainer->settings.keyboard_shortcuts.end(); ++i)
			{
				std::string keyname=Toolkit::getStringTable()->getString(("["+i->first+"]").c_str());
				std::string valname=Toolkit::getStringTable()->getString((i->second=="" ? "[unassigned]" : "["+i->second+"]").c_str());
				keyboard_shortcut_names->addText(keyname + " - " + valname);
			}
			keyboard_shortcut_names->setSelectionIndex(pos);
		}
	}
	else if (action==NUMBER_ELEMENT_SELECTED)
	{
		if (warflagUnitRatio->getNth() == 0)
		{
			warflagUnitRatio->setNth(1);
		}
		globalContainer->settings.warflagUnit=warflagUnitRatio->getNth();
		if (clearflagUnitRatio->getNth() == 0)
		{
			clearflagUnitRatio->setNth(1);
		}
		globalContainer->settings.clearflagUnit=clearflagUnitRatio->getNth();
		if (exploreflagUnitRatio->getNth() == 0)
		{
			exploreflagUnitRatio->setNth(1);
		}
		globalContainer->settings.exploreflagUnit=exploreflagUnitRatio->getNth();
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

			musicVolText->setText(Toolkit::getStringTable()->getString("[Music volume]"));
			audioMuteText->setText(Toolkit::getStringTable()->getString("[mute]"));
			
			rebootWarning->setText(Toolkit::getStringTable()->getString("[Warning, you need to reboot the game for changes to take effect]"));
		}
		else if (source==modeList)
		{
			int w, h;
			sscanf(modeList->getText(par1).c_str(), "%dx%d", &w, &h);
			globalContainer->settings.screenWidth=w;
			globalContainer->settings.screenHeight=h;
			updateGfxCtx();
		}
		else if (source==keyboard_shortcuts)
		{
			if(keyboard_shortcut_names->getSelectionIndex()!=-1)
			{
				int pos=par1;
				unsigned int change_pos=keyboard_shortcut_names->getSelectionIndex();
				std::string keyname=Toolkit::getStringTable()->getString(("["+internal_names[change_pos]+"]").c_str());
				std::string valname=Toolkit::getStringTable()->getString((shortcut_actions[pos]=="" ? "[unassigned]" : "["+shortcut_actions[pos]+"]").c_str());
				keyboard_shortcut_names->removeText(change_pos);
				if(change_pos==globalContainer->settings.keyboard_shortcuts.size()-1)
				{
					keyboard_shortcut_names->addText(keyname + " - " + valname);
				}
				else
				{
					keyboard_shortcut_names->addText(keyname + " - " + valname, change_pos);
				}
				globalContainer->settings.keyboard_shortcuts[internal_names[change_pos]]=shortcut_actions[pos];
				keyboard_shortcuts->setSelectionIndex(-1);
			}
		}
		else if (source==keyboard_shortcut_names)
		{
			keyboard_shortcuts->setSelectionIndex(-1);
		}
	}
	else if (action==VALUE_CHANGED)
	{
		globalContainer->settings.musicVolume = musicVol->getValue();
		globalContainer->mix->setVolume(globalContainer->settings.musicVolume, globalContainer->settings.mute);
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		if (source==rememberUnitButton)
		{
			globalContainer->settings.rememberUnit=rememberUnitButton->getState();
		}
		if (source==lowquality)
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
			globalContainer->mix->setVolume(globalContainer->settings.musicVolume, globalContainer->settings.mute);
			setVisibilityFromAudioSettings();
		}
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

int SettingsScreen::menu(void)
{
	return SettingsScreen().execute(globalContainer->gfx, 30);
}
