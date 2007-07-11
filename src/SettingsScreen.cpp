/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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
#include <algorithm>
#include "boost/lexical_cast.hpp"

SettingsScreen::SettingsScreen()
 : keyboardManager(KeyboardManager::GameGUIShortcuts)
{
	old_settings=globalContainer->settings;
	//following are standard choices for all screens
	//tab choices
	generalsettings=new TextButton( 10, 10, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[general settings]"), GENERALSETTINGS, 8);
	addWidget(generalsettings);

	unitsettings=new TextButton( 220, 10, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[unit settings]"), UNITSETTINGS, 9);
	addWidget(unitsettings);

	keyboardsettings=new TextButton( 430, 10, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[keyboard settings]"), KEYBOARDSETTINGS, 9);
	addWidget(keyboardsettings);

	// Screen entry/quit part
	ok=new TextButton( 230, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	addWidget(ok);
	cancel=new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
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
	musicVol=new Selector(320, 360, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 180, globalContainer->settings.musicVolume, 256);
	addWidget(musicVol);
	musicVolText=new Text(320, 330, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Music volume]"), 300);
	addWidget(musicVolText);
	setVisibilityFromAudioSettings();
	
	int row=0;
	int current_column_x=20;
	int widest_element=0;
	for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
	{
		std::string name=IntBuildingType::typeFromShortNumber(t);
		for(int l=0; l<6; ++l)
		{
			if(globalContainer->buildingsTypes.getByType(name, l/2, (l+1)%2) != NULL && globalContainer->settings.defaultUnitsAssigned[t][l]>0)
			{
				unitRatios[t][l] = new Number(current_column_x, 70+45*row, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
				addNumbersFor(0, 20, unitRatios[t][l]);
				unitRatios[t][l]->setNth(globalContainer->settings.defaultUnitsAssigned[t][l]);
				unitRatios[t][l]->visible=false;
				addWidget(unitRatios[t][l]);

				widest_element = std::max(widest_element, unitRatios[t][l]->getWidth());

				std::string keyname="[";
				if((l+1)%2)
					keyname+="build ";
				keyname+=name + " level " + boost::lexical_cast<std::string>(l/2) + "]";
				unitRatioTexts[t][l]=new Text(current_column_x, 50+45*row, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString(keyname.c_str()));
				addWidget(unitRatioTexts[t][l]);
				unitRatioTexts[t][l]->visible=false;

				widest_element = std::max(widest_element, unitRatioTexts[t][l]->getWidth());

				row+=1;
				if(row==8)
				{
					row=0;
					current_column_x+= widest_element+10;
				}
			}
			else
			{
				unitRatios[t][l]=NULL;
				unitRatioTexts[t][l]=NULL;
			}
		}
	}

	//shortcuts part
	game_shortcuts=new TextButton( 20, 60, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[game shortcuts]"), GAMESHORTCUTS);
	game_shortcuts->visible=false;

	editor_shortcuts=new TextButton( 230, 60, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[editor shortcuts]"), EDITORSHORTCUTS);
	editor_shortcuts->visible=false;
	
	keyboard_shortcut_names=new List(20, 110, 325, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	keyboard_shortcut_names->visible=false;

	keyboard_shortcuts=new List(355, 110, 275, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	keyboard_shortcuts->visible=false;

	editor_keyboard_shortcuts=new List(355, 110, 275, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	editor_keyboard_shortcuts->visible=false;

	restore_default_shortcuts = new TextButton(20, 315, 610, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[restore default shortcuts]"), RESTOREDEFAULTSHORTCUTS);
	restore_default_shortcuts->visible=false;


	addWidget(game_shortcuts);
	addWidget(editor_shortcuts);
	addWidget(keyboard_shortcut_names);
	addWidget(keyboard_shortcuts);
	addWidget(editor_keyboard_shortcuts);
	addWidget(restore_default_shortcuts);

/*
	for(std::map<std::string, std::string>::iterator i=globalContainer->settings.keyboard_shortcuts.begin(); i!=globalContainer->settings.keyboard_shortcuts.end(); ++i)
	{
		internal_names.push_back(i->first);
		std::string keyname=Toolkit::getStringTable()->getString(("["+i->first+"]").c_str());
		std::string valname=Toolkit::getStringTable()->getString((i->second=="" ? "[unassigned]" : "["+i->second+"]").c_str());
		keyboard_shortcut_names->addText(keyname + " - " + valname);
	}

	shortcut_names.push_back(Toolkit::getStringTable()->getString("[unassigned]"));
	shortcut_actions.push_back("");
	shortcut_names.push_back(Toolkit::getStringTable()->getString("[toggle draw unit paths]"));
	shortcut_actions.push_back("toggle draw unit paths");
	shortcut_names.push_back(Toolkit::getStringTable()->getString("[destroy building]"));
	shortcut_actions.push_back("destroy building");
	shortcut_names.push_back(Toolkit::getStringTable()->getString("[upgrade building]"));
	shortcut_actions.push_back("upgrade building");
	shortcut_names.push_back(Toolkit::getStringTable()->getString("[repair building]"));
	shortcut_actions.push_back("repair building");
	shortcut_names.push_back(Toolkit::getStringTable()->getString("[toggle draw information]"));
	shortcut_actions.push_back("toggle draw information");
	shortcut_names.push_back(Toolkit::getStringTable()->getString("[toggle draw accessibility aids]"));
	shortcut_actions.push_back("toggle draw accessibility aids");
	shortcut_names.push_back(Toolkit::getStringTable()->getString("[mark map]"));
	shortcut_actions.push_back("mark map");
	shortcut_names.push_back(Toolkit::getStringTable()->getString("[record voice]"));
	shortcut_actions.push_back("record voice");
	shortcut_names.push_back(Toolkit::getStringTable()->getString("[pause game]"));
	shortcut_actions.push_back("pause game");
        char * (commands[]) = {
          "prefix key select area tool",
          "prefix key select building tool",
          "prefix key select flag tool",
          "select make swarm tool",
          "select make inn tool",
          "select make hospital tool",
          "select make racetrack tool",
          "select make swimming pool tool",
          "select make barracks tool",
          "select make school tool",
          "select make defense tower tool",
          "select make stone wall tool",
          "select make market tool",
          "select make exploration flag tool",
          "select make war flag tool",
          "select make clearing flag tool",
          "select forbidden area tool",
          "select guard area tool",
          "select clearing area tool",
          "switch to adding areas",
          "switch to deleting areas",
          "switch to area brush 1",
          "switch to area brush 2",
          "switch to area brush 3",
          "switch to area brush 4",
          "switch to area brush 5",
          "switch to area brush 6",
          "switch to area brush 7",
          "switch to area brush 8", };
        // fprintf (stderr, "before loop: sizeof(commands): %d\n", sizeof (commands));
        for (int i = 0; i < (sizeof (commands) / (sizeof (commands[0]))); i++) {
          // fprintf (stderr, "i: %d\n", i);
          char buffer[100];
          snprintf (buffer, sizeof(buffer), "[%s]", commands[i]);
          buffer[99] = '\0';
          const char * message = Toolkit::getStringTable()->getString(buffer);
          // fprintf (stderr, "commands[%d]: {%s}, buffer: {%s}, message: {%s}\n", i, commands[i], buffer, message);
          shortcut_names.push_back (message);
          shortcut_actions.push_back (commands[i]); }
        // fprintf (stderr, "after loop\n");

*/
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[switch to building view]"));
	editor_shortcut_actions.push_back("switch to building view");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[switch to flag view]"));
	editor_shortcut_actions.push_back("switch to flag view");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[switch to terrain view]"));
	editor_shortcut_actions.push_back("switch to terrain view");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[switch to teams view]"));
	editor_shortcut_actions.push_back("switch to teams view");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[open save menu]"));
	editor_shortcut_actions.push_back("open save screen");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[open load menu]"));
	editor_shortcut_actions.push_back("open load screen");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select swarm building]"));
	editor_shortcut_actions.push_back("unselect&switch to building view&set place building selection swarm");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select inn building]"));
	editor_shortcut_actions.push_back("unselect&switch to building view&set place building selection inn");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select hospital building]"));
	editor_shortcut_actions.push_back("unselect&switch to building view&set place building selection hospital");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select racetrack building]"));
	editor_shortcut_actions.push_back("unselect&switch to building view&set place building selection racetrack");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select swimmingpool building]"));
	editor_shortcut_actions.push_back("unselect&switch to building view&set place building selection swimmingpool");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select school building]"));
	editor_shortcut_actions.push_back("unselect&switch to building view&set place building selection school");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select barracks building]"));
	editor_shortcut_actions.push_back("unselect&switch to building view&set place building selection barracks");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select tower building]"));
	editor_shortcut_actions.push_back("unselect&switch to building view&set place building selection tower");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select wall building]"));
	editor_shortcut_actions.push_back("unselect&switch to building view&set place building selection stonewall");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select market building]"));
	editor_shortcut_actions.push_back("unselect&switch to building view&set place building selection market");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select exploration flag]"));
	editor_shortcut_actions.push_back("unselect&switch to flag view&set place building selection explorationflag");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select war flag]"));
	editor_shortcut_actions.push_back("unselect&switch to flag view&set place building selection warflag");
	editor_shortcut_names.push_back(Toolkit::getStringTable()->getString("[select clearing flag]"));
	editor_shortcut_actions.push_back("unselect&switch to flag view&set place building selection clearingflag");

/*
	for(unsigned int x=0; x<shortcut_names.size(); ++x)
	{
		keyboard_shortcuts->addText(shortcut_names[x].c_str());
	}
	*/
	for(unsigned int x=0; x<editor_shortcut_names.size(); ++x)
	{
		editor_keyboard_shortcuts->addText(editor_shortcut_names[x].c_str());
	}


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
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==OK)
		{
			globalContainer->setUserName(userName->getText());
			globalContainer->settings.defaultLanguage = Toolkit::getStringTable()->getLang();
			globalContainer->settings.save();
			keyboardManager.saveKeyboardLayout();
			endExecute(par1);
		}
		else if (par1==CANCEL)
		{
			globalContainer->settings=old_settings;
			if (gfxAltered)
				updateGfxCtx();

			Toolkit::getStringTable()->setLang(globalContainer->settings.defaultLanguage);

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
			
			for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
			{
				for(int l=0; l<6; ++l)
				{
					if(unitRatios[t][l])
					{
						unitRatios[t][l]->visible=false;
						unitRatioTexts[t][l]->visible=false;
					}
				}
			}

			game_shortcuts->visible=false;
			editor_shortcuts->visible=false;
			keyboard_shortcut_names->visible=false;
			keyboard_shortcuts->visible=false;
			editor_keyboard_shortcuts->visible=false;
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

			for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
			{
				for(int l=0; l<6; ++l)
				{
					if(unitRatios[t][l])
					{
						unitRatios[t][l]->visible=true;
						unitRatioTexts[t][l]->visible=true;
					}
				}
			}
	
			game_shortcuts->visible=false;
			editor_shortcuts->visible=false;
			keyboard_shortcut_names->visible=false;
			keyboard_shortcuts->visible=false;
			editor_keyboard_shortcuts->visible=false;
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
			
			for(int t=0; t<IntBuildingType::NB_BUILDING; ++t)
			{
				for(int l=0; l<6; ++l)
				{
					if(unitRatios[t][l])
					{
						unitRatios[t][l]->visible=false;
						unitRatioTexts[t][l]->visible=false;
					}
				}
			}
	
			game_shortcuts->visible=true;
			editor_shortcuts->visible=true;
			keyboard_shortcut_names->visible=true;
			keyboard_shortcuts->visible=true;
			editor_keyboard_shortcuts->visible=false;
			restore_default_shortcuts->visible=true;
			reset_names();
		}
		else if (par1==RESTOREDEFAULTSHORTCUTS)
		{
			globalContainer->settings.restoreDefaultShortcuts();
			reset_names();
		}
		else if(par1==GAMESHORTCUTS)
		{
			keyboard_shortcuts->visible=true;
			editor_keyboard_shortcuts->visible=false;
			reset_names();
		}
		else if(par1==EDITORSHORTCUTS)
		{
			keyboard_shortcuts->visible=false;
			editor_keyboard_shortcuts->visible=true;
			reset_names();
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
		else if (source==keyboard_shortcuts || source==editor_keyboard_shortcuts)
		{
		/*
			List* shortcuts=(editor_keyboard_shortcuts->visible ? editor_keyboard_shortcuts : keyboard_shortcuts);
			std::map<std::string, std::string>& options=(editor_keyboard_shortcuts->visible ? globalContainer->settings.editor_keyboard_shortcuts : globalContainer->settings.keyboard_shortcuts);
			std::vector<std::string>& actions=(editor_keyboard_shortcuts->visible ? editor_shortcut_actions : shortcut_actions);
			std::vector<std::string>& names=(editor_keyboard_shortcuts->visible ? editor_shortcut_names : shortcut_names);
			if(keyboard_shortcut_names->getSelectionIndex()!=-1)
			{
				int pos=par1;
				unsigned int change_pos=keyboard_shortcut_names->getSelectionIndex();
				std::string keyname=Toolkit::getStringTable()->getString(("["+internal_names[change_pos]+"]").c_str());
				std::string valname=(actions[pos]=="" ? Toolkit::getStringTable()->getString("[unassigned]") : names[pos]);
				keyboard_shortcut_names->removeText(change_pos);
				if(change_pos==options.size()-1)
				{
					keyboard_shortcut_names->addText(keyname + " - " + valname);
				}
				else
				{
					keyboard_shortcut_names->addText(keyname + " - " + valname, change_pos);
				}
				options[internal_names[change_pos]]=actions[pos];
				shortcuts->setSelectionIndex(-1);
			}
			*/
		}
		else if (source==keyboard_shortcut_names)
		{
			List* shortcuts=(editor_keyboard_shortcuts->visible ? editor_keyboard_shortcuts : keyboard_shortcuts);
			shortcuts->setSelectionIndex(-1);
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



void SettingsScreen::reset_names()
{
/*
	std::map<std::string, std::string>& options=(editor_keyboard_shortcuts->visible ? globalContainer->settings.editor_keyboard_shortcuts : globalContainer->settings.keyboard_shortcuts);
	std::vector<std::string>& names=(editor_keyboard_shortcuts->visible ? editor_shortcut_names : shortcut_names);
	std::vector<std::string>& actions=(editor_keyboard_shortcuts->visible ? editor_shortcut_actions : shortcut_actions);

	unsigned int pos=keyboard_shortcut_names->getSelectionIndex();
	keyboard_shortcut_names->setSelectionIndex(-1);
	for(unsigned int x=0; x<options.size(); ++x)
	{
		keyboard_shortcut_names->removeText(0);
	}
	for(std::map<std::string, std::string>::iterator i=options.begin(); i!=options.end(); ++i)
	{
		std::string keyname=Toolkit::getStringTable()->getString(("["+i->first+"]").c_str());
		std::string valname=(i->second=="" ? Toolkit::getStringTable()->getString("[unassigned]") : names[std::find(actions.begin(), actions.end(), i->second)-actions.begin()]);
		keyboard_shortcut_names->addText(keyname + " - " + valname);
	}
	keyboard_shortcut_names->setSelectionIndex(pos);
*/
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
