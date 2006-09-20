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
#include <algorithm>

SettingsScreen::SettingsScreen()
{
	old_settings=globalContainer->settings;
	//following are standard choices for all screens
	//tab choices
	generalsettings=new TextButton( 10, 10, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[general settings]"), GENERALSETTINGS, 8);
	addWidget(generalsettings);

	unitsettings=new TextButton( 220, 10, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[unit settings]"), UNITSETTINGS, 9);
	addWidget(unitsettings);

	keyboardsettings=new TextButton( 430, 10, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[keyboard settings]"), KEYBOARDSETTINGS, 9);
	addWidget(keyboardsettings);

	// Screen entry/quit part
	ok=new TextButton( 230, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
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
	//unit settings part
	swarmUnitRatio0c=new Number(20, 70, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	swarmUnitRatio0c->add(0);
	swarmUnitRatio0c->add(1);
	swarmUnitRatio0c->add(2);
	swarmUnitRatio0c->add(3);
	swarmUnitRatio0c->add(4);
	swarmUnitRatio0c->add(5);
	swarmUnitRatio0c->add(6);
	swarmUnitRatio0c->add(7);
	swarmUnitRatio0c->add(8);
	swarmUnitRatio0c->add(9);
	swarmUnitRatio0c->add(10);
	swarmUnitRatio0c->add(11);
	swarmUnitRatio0c->add(12);
	swarmUnitRatio0c->add(13);
	swarmUnitRatio0c->add(14);
	swarmUnitRatio0c->add(15);
	swarmUnitRatio0c->add(16);
	swarmUnitRatio0c->add(17);
	swarmUnitRatio0c->add(18);
	swarmUnitRatio0c->add(19);
	swarmUnitRatio0c->add(20);
	swarmUnitRatio0c->setNth(globalContainer->settings.swarmUnit0c);
	swarmUnitRatio0c->visible=false;
	addWidget(swarmUnitRatio0c);
	
	swarmUnitText0c=new Text(20, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Swarm"));
	addWidget(swarmUnitText0c);
	swarmUnitText0c->visible=false;
	
	swarmUnitRatio0=new Number(20, 120, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	swarmUnitRatio0->add(0);
	swarmUnitRatio0->add(1);
	swarmUnitRatio0->add(2);
	swarmUnitRatio0->add(3);
	swarmUnitRatio0->add(4);
	swarmUnitRatio0->add(5);
	swarmUnitRatio0->add(6);
	swarmUnitRatio0->add(7);
	swarmUnitRatio0->add(8);
	swarmUnitRatio0->add(9);
	swarmUnitRatio0->add(10);
	swarmUnitRatio0->add(11);
	swarmUnitRatio0->add(12);
	swarmUnitRatio0->add(13);
	swarmUnitRatio0->add(14);
	swarmUnitRatio0->add(15);
	swarmUnitRatio0->add(16);
	swarmUnitRatio0->add(17);
	swarmUnitRatio0->add(18);
	swarmUnitRatio0->add(19);
	swarmUnitRatio0->add(20);
	swarmUnitRatio0->setNth(globalContainer->settings.swarmUnit0);
	swarmUnitRatio0->visible=false;
	addWidget(swarmUnitRatio0);
	
	swarmUnitText0=new Text(20, 100, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Swarm"));
	addWidget(swarmUnitText0);
	swarmUnitText0->visible=false;
	
	innUnitRatio0c=new Number(20, 170, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	innUnitRatio0c->add(0);
	innUnitRatio0c->add(1);
	innUnitRatio0c->add(2);
	innUnitRatio0c->add(3);
	innUnitRatio0c->add(4);
	innUnitRatio0c->add(5);
	innUnitRatio0c->add(6);
	innUnitRatio0c->add(7);
	innUnitRatio0c->add(8);
	innUnitRatio0c->add(9);
	innUnitRatio0c->add(10);
	innUnitRatio0c->add(11);
	innUnitRatio0c->add(12);
	innUnitRatio0c->add(13);
	innUnitRatio0c->add(14);
	innUnitRatio0c->add(15);
	innUnitRatio0c->add(16);
	innUnitRatio0c->add(17);
	innUnitRatio0c->add(18);
	innUnitRatio0c->add(19);
	innUnitRatio0c->add(20);
	innUnitRatio0c->setNth(globalContainer->settings.innUnit0c);
	innUnitRatio0c->visible=false;
	addWidget(innUnitRatio0c);
	
	innUnitText0c=new Text(20, 150, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Inn 1"));
	addWidget(innUnitText0c);
	innUnitText0c->visible=false;
	
	innUnitRatio0=new Number(20, 220, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	innUnitRatio0->add(0);
	innUnitRatio0->add(1);
	innUnitRatio0->add(2);
	innUnitRatio0->add(3);
	innUnitRatio0->add(4);
	innUnitRatio0->add(5);
	innUnitRatio0->add(6);
	innUnitRatio0->add(7);
	innUnitRatio0->add(8);
	innUnitRatio0->add(9);
	innUnitRatio0->add(10);
	innUnitRatio0->add(11);
	innUnitRatio0->add(12);
	innUnitRatio0->add(13);
	innUnitRatio0->add(14);
	innUnitRatio0->add(15);
	innUnitRatio0->add(16);
	innUnitRatio0->add(17);
	innUnitRatio0->add(18);
	innUnitRatio0->add(19);
	innUnitRatio0->add(20);
	innUnitRatio0->setNth(globalContainer->settings.innUnit0);
	innUnitRatio0->visible=false;
	addWidget(innUnitRatio0);
	
	innUnitText0=new Text(20, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Inn 1"));
	addWidget(innUnitText0);
	innUnitText0->visible=false;
	
	innUnitRatio1c=new Number(20, 270, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	innUnitRatio1c->add(0);
	innUnitRatio1c->add(1);
	innUnitRatio1c->add(2);
	innUnitRatio1c->add(3);
	innUnitRatio1c->add(4);
	innUnitRatio1c->add(5);
	innUnitRatio1c->add(6);
	innUnitRatio1c->add(7);
	innUnitRatio1c->add(8);
	innUnitRatio1c->add(9);
	innUnitRatio1c->add(10);
	innUnitRatio1c->add(11);
	innUnitRatio1c->add(12);
	innUnitRatio1c->add(13);
	innUnitRatio1c->add(14);
	innUnitRatio1c->add(15);
	innUnitRatio1c->add(16);
	innUnitRatio1c->add(17);
	innUnitRatio1c->add(18);
	innUnitRatio1c->add(19);
	innUnitRatio1c->add(20);
	innUnitRatio1c->setNth(globalContainer->settings.innUnit1c);
	innUnitRatio1c->visible=false;
	addWidget(innUnitRatio1c);
	
	innUnitText1c=new Text(20, 250, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Inn 2"));
	addWidget(innUnitText1c);
	innUnitText1c->visible=false;
	
	innUnitRatio1=new Number(20, 320, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	innUnitRatio1->add(0);
	innUnitRatio1->add(1);
	innUnitRatio1->add(2);
	innUnitRatio1->add(3);
	innUnitRatio1->add(4);
	innUnitRatio1->add(5);
	innUnitRatio1->add(6);
	innUnitRatio1->add(7);
	innUnitRatio1->add(8);
	innUnitRatio1->add(9);
	innUnitRatio1->add(10);
	innUnitRatio1->add(11);
	innUnitRatio1->add(12);
	innUnitRatio1->add(13);
	innUnitRatio1->add(14);
	innUnitRatio1->add(15);
	innUnitRatio1->add(16);
	innUnitRatio1->add(17);
	innUnitRatio1->add(18);
	innUnitRatio1->add(19);
	innUnitRatio1->add(20);
	innUnitRatio1->setNth(globalContainer->settings.innUnit1);
	innUnitRatio1->visible=false;
	addWidget(innUnitRatio1);
	
	innUnitText1=new Text(20, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Inn 2"));
	addWidget(innUnitText1);
	innUnitText1->visible=false;
	
	innUnitRatio2c=new Number(20, 370, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	innUnitRatio2c->add(0);
	innUnitRatio2c->add(1);
	innUnitRatio2c->add(2);
	innUnitRatio2c->add(3);
	innUnitRatio2c->add(4);
	innUnitRatio2c->add(5);
	innUnitRatio2c->add(6);
	innUnitRatio2c->add(7);
	innUnitRatio2c->add(8);
	innUnitRatio2c->add(9);
	innUnitRatio2c->add(10);
	innUnitRatio2c->add(11);
	innUnitRatio2c->add(12);
	innUnitRatio2c->add(13);
	innUnitRatio2c->add(14);
	innUnitRatio2c->add(15);
	innUnitRatio2c->add(16);
	innUnitRatio2c->add(17);
	innUnitRatio2c->add(18);
	innUnitRatio2c->add(19);
	innUnitRatio2c->add(20);
	innUnitRatio2c->setNth(globalContainer->settings.innUnit2c);
	innUnitRatio2c->visible=false;
	addWidget(innUnitRatio2c);
	
	innUnitText2c=new Text(20, 350, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Inn 3"));
	addWidget(innUnitText2c);
	innUnitText2c->visible=false;
	
	innUnitRatio2=new Number(140, 70, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	innUnitRatio2->add(0);
	innUnitRatio2->add(1);
	innUnitRatio2->add(2);
	innUnitRatio2->add(3);
	innUnitRatio2->add(4);
	innUnitRatio2->add(5);
	innUnitRatio2->add(6);
	innUnitRatio2->add(7);
	innUnitRatio2->add(8);
	innUnitRatio2->add(9);
	innUnitRatio2->add(10);
	innUnitRatio2->add(11);
	innUnitRatio2->add(12);
	innUnitRatio2->add(13);
	innUnitRatio2->add(14);
	innUnitRatio2->add(15);
	innUnitRatio2->add(16);
	innUnitRatio2->add(17);
	innUnitRatio2->add(18);
	innUnitRatio2->add(19);
	innUnitRatio2->add(20);
	innUnitRatio2->setNth(globalContainer->settings.innUnit2);
	innUnitRatio2->visible=false;
	addWidget(innUnitRatio2);
	
	innUnitText2=new Text(140, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Inn 3"));
	addWidget(innUnitText2);
	innUnitText2->visible=false;
	
	hospitalUnitRatio0c=new Number(140, 120, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	hospitalUnitRatio0c->add(0);
	hospitalUnitRatio0c->add(1);
	hospitalUnitRatio0c->add(2);
	hospitalUnitRatio0c->add(3);
	hospitalUnitRatio0c->add(4);
	hospitalUnitRatio0c->add(5);
	hospitalUnitRatio0c->add(6);
	hospitalUnitRatio0c->add(7);
	hospitalUnitRatio0c->add(8);
	hospitalUnitRatio0c->add(9);
	hospitalUnitRatio0c->add(10);
	hospitalUnitRatio0c->add(11);
	hospitalUnitRatio0c->add(12);
	hospitalUnitRatio0c->add(13);
	hospitalUnitRatio0c->add(14);
	hospitalUnitRatio0c->add(15);
	hospitalUnitRatio0c->add(16);
	hospitalUnitRatio0c->add(17);
	hospitalUnitRatio0c->add(18);
	hospitalUnitRatio0c->add(19);
	hospitalUnitRatio0c->add(20);
	hospitalUnitRatio0c->setNth(globalContainer->settings.innUnit1c);
	hospitalUnitRatio0c->visible=false;
	addWidget(hospitalUnitRatio0c);
	
	hospitalUnitText0c=new Text(140, 100, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Hospital 1"));
	addWidget(hospitalUnitText0c);
	hospitalUnitText0c->visible=false;
	
	hospitalUnitRatio1c=new Number(140, 170, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	hospitalUnitRatio1c->add(0);
	hospitalUnitRatio1c->add(1);
	hospitalUnitRatio1c->add(2);
	hospitalUnitRatio1c->add(3);
	hospitalUnitRatio1c->add(4);
	hospitalUnitRatio1c->add(5);
	hospitalUnitRatio1c->add(6);
	hospitalUnitRatio1c->add(7);
	hospitalUnitRatio1c->add(8);
	hospitalUnitRatio1c->add(9);
	hospitalUnitRatio1c->add(10);
	hospitalUnitRatio1c->add(11);
	hospitalUnitRatio1c->add(12);
	hospitalUnitRatio1c->add(13);
	hospitalUnitRatio1c->add(14);
	hospitalUnitRatio1c->add(15);
	hospitalUnitRatio1c->add(16);
	hospitalUnitRatio1c->add(17);
	hospitalUnitRatio1c->add(18);
	hospitalUnitRatio1c->add(19);
	hospitalUnitRatio1c->add(20);
	hospitalUnitRatio1c->setNth(globalContainer->settings.innUnit1c);
	hospitalUnitRatio1c->visible=false;
	addWidget(hospitalUnitRatio1c);
	
	hospitalUnitText1c=new Text(140, 150, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Hospital 2"));
	addWidget(hospitalUnitText1c);
	hospitalUnitText1c->visible=false;
	
	hospitalUnitRatio2c=new Number(140, 220, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	hospitalUnitRatio2c->add(0);
	hospitalUnitRatio2c->add(1);
	hospitalUnitRatio2c->add(2);
	hospitalUnitRatio2c->add(3);
	hospitalUnitRatio2c->add(4);
	hospitalUnitRatio2c->add(5);
	hospitalUnitRatio2c->add(6);
	hospitalUnitRatio2c->add(7);
	hospitalUnitRatio2c->add(8);
	hospitalUnitRatio2c->add(9);
	hospitalUnitRatio2c->add(10);
	hospitalUnitRatio2c->add(11);
	hospitalUnitRatio2c->add(12);
	hospitalUnitRatio2c->add(13);
	hospitalUnitRatio2c->add(14);
	hospitalUnitRatio2c->add(15);
	hospitalUnitRatio2c->add(16);
	hospitalUnitRatio2c->add(17);
	hospitalUnitRatio2c->add(18);
	hospitalUnitRatio2c->add(19);
	hospitalUnitRatio2c->add(20);
	hospitalUnitRatio2c->setNth(globalContainer->settings.innUnit1c);
	hospitalUnitRatio2c->visible=false;
	addWidget(hospitalUnitRatio2c);
	
	hospitalUnitText2c=new Text(140, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Hospital 3"));
	addWidget(hospitalUnitText2c);
	hospitalUnitText2c->visible=false;
	
	racetrackUnitRatio0c=new Number(140, 270, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	racetrackUnitRatio0c->add(0);
	racetrackUnitRatio0c->add(1);
	racetrackUnitRatio0c->add(2);
	racetrackUnitRatio0c->add(3);
	racetrackUnitRatio0c->add(4);
	racetrackUnitRatio0c->add(5);
	racetrackUnitRatio0c->add(6);
	racetrackUnitRatio0c->add(7);
	racetrackUnitRatio0c->add(8);
	racetrackUnitRatio0c->add(9);
	racetrackUnitRatio0c->add(10);
	racetrackUnitRatio0c->add(11);
	racetrackUnitRatio0c->add(12);
	racetrackUnitRatio0c->add(13);
	racetrackUnitRatio0c->add(14);
	racetrackUnitRatio0c->add(15);
	racetrackUnitRatio0c->add(16);
	racetrackUnitRatio0c->add(17);
	racetrackUnitRatio0c->add(18);
	racetrackUnitRatio0c->add(19);
	racetrackUnitRatio0c->add(20);
	racetrackUnitRatio0c->setNth(globalContainer->settings.innUnit1c);
	racetrackUnitRatio0c->visible=false;
	addWidget(racetrackUnitRatio0c);
	
	racetrackUnitText0c=new Text(140, 250, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Track 1"));
	addWidget(racetrackUnitText0c);
	racetrackUnitText0c->visible=false;
	
	racetrackUnitRatio1c=new Number(140, 320, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	racetrackUnitRatio1c->add(0);
	racetrackUnitRatio1c->add(1);
	racetrackUnitRatio1c->add(2);
	racetrackUnitRatio1c->add(3);
	racetrackUnitRatio1c->add(4);
	racetrackUnitRatio1c->add(5);
	racetrackUnitRatio1c->add(6);
	racetrackUnitRatio1c->add(7);
	racetrackUnitRatio1c->add(8);
	racetrackUnitRatio1c->add(9);
	racetrackUnitRatio1c->add(10);
	racetrackUnitRatio1c->add(11);
	racetrackUnitRatio1c->add(12);
	racetrackUnitRatio1c->add(13);
	racetrackUnitRatio1c->add(14);
	racetrackUnitRatio1c->add(15);
	racetrackUnitRatio1c->add(16);
	racetrackUnitRatio1c->add(17);
	racetrackUnitRatio1c->add(18);
	racetrackUnitRatio1c->add(19);
	racetrackUnitRatio1c->add(20);
	racetrackUnitRatio1c->setNth(globalContainer->settings.innUnit1c);
	racetrackUnitRatio1c->visible=false;
	addWidget(racetrackUnitRatio1c);
	
	racetrackUnitText1c=new Text(140, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Track 2"));
	addWidget(racetrackUnitText1c);
	racetrackUnitText1c->visible=false;
	
	racetrackUnitRatio2c=new Number(140, 370, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	racetrackUnitRatio2c->add(0);
	racetrackUnitRatio2c->add(1);
	racetrackUnitRatio2c->add(2);
	racetrackUnitRatio2c->add(3);
	racetrackUnitRatio2c->add(4);
	racetrackUnitRatio2c->add(5);
	racetrackUnitRatio2c->add(6);
	racetrackUnitRatio2c->add(7);
	racetrackUnitRatio2c->add(8);
	racetrackUnitRatio2c->add(9);
	racetrackUnitRatio2c->add(10);
	racetrackUnitRatio2c->add(11);
	racetrackUnitRatio2c->add(12);
	racetrackUnitRatio2c->add(13);
	racetrackUnitRatio2c->add(14);
	racetrackUnitRatio2c->add(15);
	racetrackUnitRatio2c->add(16);
	racetrackUnitRatio2c->add(17);
	racetrackUnitRatio2c->add(18);
	racetrackUnitRatio2c->add(19);
	racetrackUnitRatio2c->add(20);
	racetrackUnitRatio2c->setNth(globalContainer->settings.innUnit1c);
	racetrackUnitRatio2c->visible=false;
	addWidget(racetrackUnitRatio2c);
	
	racetrackUnitText2c=new Text(140, 350, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Track 3"));
	addWidget(racetrackUnitText2c);
	racetrackUnitText2c->visible=false;
	
	swimmingpoolUnitRatio0c=new Number(260, 70, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	swimmingpoolUnitRatio0c->add(0);
	swimmingpoolUnitRatio0c->add(1);
	swimmingpoolUnitRatio0c->add(2);
	swimmingpoolUnitRatio0c->add(3);
	swimmingpoolUnitRatio0c->add(4);
	swimmingpoolUnitRatio0c->add(5);
	swimmingpoolUnitRatio0c->add(6);
	swimmingpoolUnitRatio0c->add(7);
	swimmingpoolUnitRatio0c->add(8);
	swimmingpoolUnitRatio0c->add(9);
	swimmingpoolUnitRatio0c->add(10);
	swimmingpoolUnitRatio0c->add(11);
	swimmingpoolUnitRatio0c->add(12);
	swimmingpoolUnitRatio0c->add(13);
	swimmingpoolUnitRatio0c->add(14);
	swimmingpoolUnitRatio0c->add(15);
	swimmingpoolUnitRatio0c->add(16);
	swimmingpoolUnitRatio0c->add(17);
	swimmingpoolUnitRatio0c->add(18);
	swimmingpoolUnitRatio0c->add(19);
	swimmingpoolUnitRatio0c->add(20);
	swimmingpoolUnitRatio0c->setNth(globalContainer->settings.innUnit1c);
	swimmingpoolUnitRatio0c->visible=false;
	addWidget(swimmingpoolUnitRatio0c);
	
	swimmingpoolUnitText0c=new Text(260, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Pool 1"));
	addWidget(swimmingpoolUnitText0c);
	swimmingpoolUnitText0c->visible=false;
	
	swimmingpoolUnitRatio1c=new Number(260, 120, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	swimmingpoolUnitRatio1c->add(0);
	swimmingpoolUnitRatio1c->add(1);
	swimmingpoolUnitRatio1c->add(2);
	swimmingpoolUnitRatio1c->add(3);
	swimmingpoolUnitRatio1c->add(4);
	swimmingpoolUnitRatio1c->add(5);
	swimmingpoolUnitRatio1c->add(6);
	swimmingpoolUnitRatio1c->add(7);
	swimmingpoolUnitRatio1c->add(8);
	swimmingpoolUnitRatio1c->add(9);
	swimmingpoolUnitRatio1c->add(10);
	swimmingpoolUnitRatio1c->add(11);
	swimmingpoolUnitRatio1c->add(12);
	swimmingpoolUnitRatio1c->add(13);
	swimmingpoolUnitRatio1c->add(14);
	swimmingpoolUnitRatio1c->add(15);
	swimmingpoolUnitRatio1c->add(16);
	swimmingpoolUnitRatio1c->add(17);
	swimmingpoolUnitRatio1c->add(18);
	swimmingpoolUnitRatio1c->add(19);
	swimmingpoolUnitRatio1c->add(20);
	swimmingpoolUnitRatio1c->setNth(globalContainer->settings.innUnit1c);
	swimmingpoolUnitRatio1c->visible=false;
	addWidget(swimmingpoolUnitRatio1c);
	
	swimmingpoolUnitText1c=new Text(260, 100, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Pool 2"));
	addWidget(swimmingpoolUnitText1c);
	swimmingpoolUnitText1c->visible=false;
	
	swimmingpoolUnitRatio2c=new Number(260, 170, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	swimmingpoolUnitRatio2c->add(0);
	swimmingpoolUnitRatio2c->add(1);
	swimmingpoolUnitRatio2c->add(2);
	swimmingpoolUnitRatio2c->add(3);
	swimmingpoolUnitRatio2c->add(4);
	swimmingpoolUnitRatio2c->add(5);
	swimmingpoolUnitRatio2c->add(6);
	swimmingpoolUnitRatio2c->add(7);
	swimmingpoolUnitRatio2c->add(8);
	swimmingpoolUnitRatio2c->add(9);
	swimmingpoolUnitRatio2c->add(10);
	swimmingpoolUnitRatio2c->add(11);
	swimmingpoolUnitRatio2c->add(12);
	swimmingpoolUnitRatio2c->add(13);
	swimmingpoolUnitRatio2c->add(14);
	swimmingpoolUnitRatio2c->add(15);
	swimmingpoolUnitRatio2c->add(16);
	swimmingpoolUnitRatio2c->add(17);
	swimmingpoolUnitRatio2c->add(18);
	swimmingpoolUnitRatio2c->add(19);
	swimmingpoolUnitRatio2c->add(20);
	swimmingpoolUnitRatio2c->setNth(globalContainer->settings.innUnit1c);
	swimmingpoolUnitRatio2c->visible=false;
	addWidget(swimmingpoolUnitRatio2c);
	
	swimmingpoolUnitText2c=new Text(260, 150, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Pool 3"));
	addWidget(swimmingpoolUnitText2c);
	swimmingpoolUnitText2c->visible=false;
	
	barracksUnitRatio0c=new Number(260, 220, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	barracksUnitRatio0c->add(0);
	barracksUnitRatio0c->add(1);
	barracksUnitRatio0c->add(2);
	barracksUnitRatio0c->add(3);
	barracksUnitRatio0c->add(4);
	barracksUnitRatio0c->add(5);
	barracksUnitRatio0c->add(6);
	barracksUnitRatio0c->add(7);
	barracksUnitRatio0c->add(8);
	barracksUnitRatio0c->add(9);
	barracksUnitRatio0c->add(10);
	barracksUnitRatio0c->add(11);
	barracksUnitRatio0c->add(12);
	barracksUnitRatio0c->add(13);
	barracksUnitRatio0c->add(14);
	barracksUnitRatio0c->add(15);
	barracksUnitRatio0c->add(16);
	barracksUnitRatio0c->add(17);
	barracksUnitRatio0c->add(18);
	barracksUnitRatio0c->add(19);
	barracksUnitRatio0c->add(20);
	barracksUnitRatio0c->setNth(globalContainer->settings.innUnit1c);
	barracksUnitRatio0c->visible=false;
	addWidget(barracksUnitRatio0c);
	
	barracksUnitText0c=new Text(260, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Barracks 1"));
	addWidget(barracksUnitText0c);
	barracksUnitText0c->visible=false;
	
	barracksUnitRatio1c=new Number(260, 270, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	barracksUnitRatio1c->add(0);
	barracksUnitRatio1c->add(1);
	barracksUnitRatio1c->add(2);
	barracksUnitRatio1c->add(3);
	barracksUnitRatio1c->add(4);
	barracksUnitRatio1c->add(5);
	barracksUnitRatio1c->add(6);
	barracksUnitRatio1c->add(7);
	barracksUnitRatio1c->add(8);
	barracksUnitRatio1c->add(9);
	barracksUnitRatio1c->add(10);
	barracksUnitRatio1c->add(11);
	barracksUnitRatio1c->add(12);
	barracksUnitRatio1c->add(13);
	barracksUnitRatio1c->add(14);
	barracksUnitRatio1c->add(15);
	barracksUnitRatio1c->add(16);
	barracksUnitRatio1c->add(17);
	barracksUnitRatio1c->add(18);
	barracksUnitRatio1c->add(19);
	barracksUnitRatio1c->add(20);
	barracksUnitRatio1c->setNth(globalContainer->settings.innUnit1c);
	barracksUnitRatio1c->visible=false;
	addWidget(barracksUnitRatio1c);
	
	barracksUnitText1c=new Text(260, 250, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Barracks 2"));
	addWidget(barracksUnitText1c);
	barracksUnitText1c->visible=false;
	
	barracksUnitRatio2c=new Number(260, 320, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	barracksUnitRatio2c->add(0);
	barracksUnitRatio2c->add(1);
	barracksUnitRatio2c->add(2);
	barracksUnitRatio2c->add(3);
	barracksUnitRatio2c->add(4);
	barracksUnitRatio2c->add(5);
	barracksUnitRatio2c->add(6);
	barracksUnitRatio2c->add(7);
	barracksUnitRatio2c->add(8);
	barracksUnitRatio2c->add(9);
	barracksUnitRatio2c->add(10);
	barracksUnitRatio2c->add(11);
	barracksUnitRatio2c->add(12);
	barracksUnitRatio2c->add(13);
	barracksUnitRatio2c->add(14);
	barracksUnitRatio2c->add(15);
	barracksUnitRatio2c->add(16);
	barracksUnitRatio2c->add(17);
	barracksUnitRatio2c->add(18);
	barracksUnitRatio2c->add(19);
	barracksUnitRatio2c->add(20);
	barracksUnitRatio2c->setNth(globalContainer->settings.innUnit1c);
	barracksUnitRatio2c->visible=false;
	addWidget(barracksUnitRatio2c);
	
	barracksUnitText2c=new Text(260, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Barracks 3"));
	addWidget(barracksUnitText2c);
	barracksUnitText2c->visible=false;
	
	schoolUnitRatio0c=new Number(260, 370, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	schoolUnitRatio0c->add(0);
	schoolUnitRatio0c->add(1);
	schoolUnitRatio0c->add(2);
	schoolUnitRatio0c->add(3);
	schoolUnitRatio0c->add(4);
	schoolUnitRatio0c->add(5);
	schoolUnitRatio0c->add(6);
	schoolUnitRatio0c->add(7);
	schoolUnitRatio0c->add(8);
	schoolUnitRatio0c->add(9);
	schoolUnitRatio0c->add(10);
	schoolUnitRatio0c->add(11);
	schoolUnitRatio0c->add(12);
	schoolUnitRatio0c->add(13);
	schoolUnitRatio0c->add(14);
	schoolUnitRatio0c->add(15);
	schoolUnitRatio0c->add(16);
	schoolUnitRatio0c->add(17);
	schoolUnitRatio0c->add(18);
	schoolUnitRatio0c->add(19);
	schoolUnitRatio0c->add(20);
	schoolUnitRatio0c->setNth(globalContainer->settings.innUnit1c);
	schoolUnitRatio0c->visible=false;
	addWidget(schoolUnitRatio0c);
	
	schoolUnitText0c=new Text(260, 350, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build School 1"));
	addWidget(schoolUnitText0c);
	schoolUnitText0c->visible=false;
	
	schoolUnitRatio1c=new Number(380, 70, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	schoolUnitRatio1c->add(0);
	schoolUnitRatio1c->add(1);
	schoolUnitRatio1c->add(2);
	schoolUnitRatio1c->add(3);
	schoolUnitRatio1c->add(4);
	schoolUnitRatio1c->add(5);
	schoolUnitRatio1c->add(6);
	schoolUnitRatio1c->add(7);
	schoolUnitRatio1c->add(8);
	schoolUnitRatio1c->add(9);
	schoolUnitRatio1c->add(10);
	schoolUnitRatio1c->add(11);
	schoolUnitRatio1c->add(12);
	schoolUnitRatio1c->add(13);
	schoolUnitRatio1c->add(14);
	schoolUnitRatio1c->add(15);
	schoolUnitRatio1c->add(16);
	schoolUnitRatio1c->add(17);
	schoolUnitRatio1c->add(18);
	schoolUnitRatio1c->add(19);
	schoolUnitRatio1c->add(20);
	schoolUnitRatio1c->setNth(globalContainer->settings.innUnit1c);
	schoolUnitRatio1c->visible=false;
	addWidget(schoolUnitRatio1c);
	
	schoolUnitText1c=new Text(380, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build School 2"));
	addWidget(schoolUnitText1c);
	schoolUnitText1c->visible=false;
	
	schoolUnitRatio2c=new Number(380, 120, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	schoolUnitRatio2c->add(0);
	schoolUnitRatio2c->add(1);
	schoolUnitRatio2c->add(2);
	schoolUnitRatio2c->add(3);
	schoolUnitRatio2c->add(4);
	schoolUnitRatio2c->add(5);
	schoolUnitRatio2c->add(6);
	schoolUnitRatio2c->add(7);
	schoolUnitRatio2c->add(8);
	schoolUnitRatio2c->add(9);
	schoolUnitRatio2c->add(10);
	schoolUnitRatio2c->add(11);
	schoolUnitRatio2c->add(12);
	schoolUnitRatio2c->add(13);
	schoolUnitRatio2c->add(14);
	schoolUnitRatio2c->add(15);
	schoolUnitRatio2c->add(16);
	schoolUnitRatio2c->add(17);
	schoolUnitRatio2c->add(18);
	schoolUnitRatio2c->add(19);
	schoolUnitRatio2c->add(20);
	schoolUnitRatio2c->setNth(globalContainer->settings.innUnit1c);
	schoolUnitRatio2c->visible=false;
	addWidget(schoolUnitRatio2c);
	
	schoolUnitText2c=new Text(380, 100, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build School 3"));
	addWidget(schoolUnitText2c);
	schoolUnitText2c->visible=false;
	
	defencetowerUnitRatio0c=new Number(380, 170, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	defencetowerUnitRatio0c->add(0);
	defencetowerUnitRatio0c->add(1);
	defencetowerUnitRatio0c->add(2);
	defencetowerUnitRatio0c->add(3);
	defencetowerUnitRatio0c->add(4);
	defencetowerUnitRatio0c->add(5);
	defencetowerUnitRatio0c->add(6);
	defencetowerUnitRatio0c->add(7);
	defencetowerUnitRatio0c->add(8);
	defencetowerUnitRatio0c->add(9);
	defencetowerUnitRatio0c->add(10);
	defencetowerUnitRatio0c->add(11);
	defencetowerUnitRatio0c->add(12);
	defencetowerUnitRatio0c->add(13);
	defencetowerUnitRatio0c->add(14);
	defencetowerUnitRatio0c->add(15);
	defencetowerUnitRatio0c->add(16);
	defencetowerUnitRatio0c->add(17);
	defencetowerUnitRatio0c->add(18);
	defencetowerUnitRatio0c->add(19);
	defencetowerUnitRatio0c->add(20);
	defencetowerUnitRatio0c->setNth(globalContainer->settings.innUnit1c);
	defencetowerUnitRatio0c->visible=false;
	addWidget(defencetowerUnitRatio0c);
	
	defencetowerUnitText0c=new Text(380, 150, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Tower 1"));
	addWidget(defencetowerUnitText0c);
	defencetowerUnitText0c->visible=false;
	
	defencetowerUnitRatio0=new Number(380, 220, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	defencetowerUnitRatio0->add(0);
	defencetowerUnitRatio0->add(1);
	defencetowerUnitRatio0->add(2);
	defencetowerUnitRatio0->add(3);
	defencetowerUnitRatio0->add(4);
	defencetowerUnitRatio0->add(5);
	defencetowerUnitRatio0->add(6);
	defencetowerUnitRatio0->add(7);
	defencetowerUnitRatio0->add(8);
	defencetowerUnitRatio0->add(9);
	defencetowerUnitRatio0->add(10);
	defencetowerUnitRatio0->add(11);
	defencetowerUnitRatio0->add(12);
	defencetowerUnitRatio0->add(13);
	defencetowerUnitRatio0->add(14);
	defencetowerUnitRatio0->add(15);
	defencetowerUnitRatio0->add(16);
	defencetowerUnitRatio0->add(17);
	defencetowerUnitRatio0->add(18);
	defencetowerUnitRatio0->add(19);
	defencetowerUnitRatio0->add(20);
	defencetowerUnitRatio0->setNth(globalContainer->settings.innUnit1c);
	defencetowerUnitRatio0->visible=false;
	addWidget(defencetowerUnitRatio0);
	
	defencetowerUnitText0=new Text(380, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Tower 1"));
	addWidget(defencetowerUnitText0);
	defencetowerUnitText0->visible=false;
	
	defencetowerUnitRatio1c=new Number(380, 270, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	defencetowerUnitRatio1c->add(0);
	defencetowerUnitRatio1c->add(1);
	defencetowerUnitRatio1c->add(2);
	defencetowerUnitRatio1c->add(3);
	defencetowerUnitRatio1c->add(4);
	defencetowerUnitRatio1c->add(5);
	defencetowerUnitRatio1c->add(6);
	defencetowerUnitRatio1c->add(7);
	defencetowerUnitRatio1c->add(8);
	defencetowerUnitRatio1c->add(9);
	defencetowerUnitRatio1c->add(10);
	defencetowerUnitRatio1c->add(11);
	defencetowerUnitRatio1c->add(12);
	defencetowerUnitRatio1c->add(13);
	defencetowerUnitRatio1c->add(14);
	defencetowerUnitRatio1c->add(15);
	defencetowerUnitRatio1c->add(16);
	defencetowerUnitRatio1c->add(17);
	defencetowerUnitRatio1c->add(18);
	defencetowerUnitRatio1c->add(19);
	defencetowerUnitRatio1c->add(20);
	defencetowerUnitRatio1c->setNth(globalContainer->settings.innUnit1c);
	defencetowerUnitRatio1c->visible=false;
	addWidget(defencetowerUnitRatio1c);
	
	defencetowerUnitText1c=new Text(380, 250, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Tower 2"));
	addWidget(defencetowerUnitText1c);
	defencetowerUnitText1c->visible=false;
	
	defencetowerUnitRatio1=new Number(380, 320, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	defencetowerUnitRatio1->add(0);
	defencetowerUnitRatio1->add(1);
	defencetowerUnitRatio1->add(2);
	defencetowerUnitRatio1->add(3);
	defencetowerUnitRatio1->add(4);
	defencetowerUnitRatio1->add(5);
	defencetowerUnitRatio1->add(6);
	defencetowerUnitRatio1->add(7);
	defencetowerUnitRatio1->add(8);
	defencetowerUnitRatio1->add(9);
	defencetowerUnitRatio1->add(10);
	defencetowerUnitRatio1->add(11);
	defencetowerUnitRatio1->add(12);
	defencetowerUnitRatio1->add(13);
	defencetowerUnitRatio1->add(14);
	defencetowerUnitRatio1->add(15);
	defencetowerUnitRatio1->add(16);
	defencetowerUnitRatio1->add(17);
	defencetowerUnitRatio1->add(18);
	defencetowerUnitRatio1->add(19);
	defencetowerUnitRatio1->add(20);
	defencetowerUnitRatio1->setNth(globalContainer->settings.innUnit1c);
	defencetowerUnitRatio1->visible=false;
	addWidget(defencetowerUnitRatio1);
	
	defencetowerUnitText1=new Text(380, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Tower 2"));
	addWidget(defencetowerUnitText1);
	defencetowerUnitText1->visible=false;
	
	defencetowerUnitRatio2c=new Number(380, 370, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	defencetowerUnitRatio2c->add(0);
	defencetowerUnitRatio2c->add(1);
	defencetowerUnitRatio2c->add(2);
	defencetowerUnitRatio2c->add(3);
	defencetowerUnitRatio2c->add(4);
	defencetowerUnitRatio2c->add(5);
	defencetowerUnitRatio2c->add(6);
	defencetowerUnitRatio2c->add(7);
	defencetowerUnitRatio2c->add(8);
	defencetowerUnitRatio2c->add(9);
	defencetowerUnitRatio2c->add(10);
	defencetowerUnitRatio2c->add(11);
	defencetowerUnitRatio2c->add(12);
	defencetowerUnitRatio2c->add(13);
	defencetowerUnitRatio2c->add(14);
	defencetowerUnitRatio2c->add(15);
	defencetowerUnitRatio2c->add(16);
	defencetowerUnitRatio2c->add(17);
	defencetowerUnitRatio2c->add(18);
	defencetowerUnitRatio2c->add(19);
	defencetowerUnitRatio2c->add(20);
	defencetowerUnitRatio2c->setNth(globalContainer->settings.innUnit1c);
	defencetowerUnitRatio2c->visible=false;
	addWidget(defencetowerUnitRatio2c);
	
	defencetowerUnitText2c=new Text(380, 350, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Tower 3"));
	addWidget(defencetowerUnitText2c);
	defencetowerUnitText2c->visible=false;
	
	defencetowerUnitRatio2=new Number(500, 70, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	defencetowerUnitRatio2->add(0);
	defencetowerUnitRatio2->add(1);
	defencetowerUnitRatio2->add(2);
	defencetowerUnitRatio2->add(3);
	defencetowerUnitRatio2->add(4);
	defencetowerUnitRatio2->add(5);
	defencetowerUnitRatio2->add(6);
	defencetowerUnitRatio2->add(7);
	defencetowerUnitRatio2->add(8);
	defencetowerUnitRatio2->add(9);
	defencetowerUnitRatio2->add(10);
	defencetowerUnitRatio2->add(11);
	defencetowerUnitRatio2->add(12);
	defencetowerUnitRatio2->add(13);
	defencetowerUnitRatio2->add(14);
	defencetowerUnitRatio2->add(15);
	defencetowerUnitRatio2->add(16);
	defencetowerUnitRatio2->add(17);
	defencetowerUnitRatio2->add(18);
	defencetowerUnitRatio2->add(19);
	defencetowerUnitRatio2->add(20);
	defencetowerUnitRatio2->setNth(globalContainer->settings.innUnit1c);
	defencetowerUnitRatio2->visible=false;
	addWidget(defencetowerUnitRatio2);
	
	defencetowerUnitText2=new Text(500, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Tower 3"));
	addWidget(defencetowerUnitText2);
	defencetowerUnitText2->visible=false;
	
	stonewallUnitRatio0c=new Number(500, 120, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	stonewallUnitRatio0c->add(0);
	stonewallUnitRatio0c->add(1);
	stonewallUnitRatio0c->add(2);
	stonewallUnitRatio0c->add(3);
	stonewallUnitRatio0c->add(4);
	stonewallUnitRatio0c->add(5);
	stonewallUnitRatio0c->add(6);
	stonewallUnitRatio0c->add(7);
	stonewallUnitRatio0c->add(8);
	stonewallUnitRatio0c->add(9);
	stonewallUnitRatio0c->add(10);
	stonewallUnitRatio0c->add(11);
	stonewallUnitRatio0c->add(12);
	stonewallUnitRatio0c->add(13);
	stonewallUnitRatio0c->add(14);
	stonewallUnitRatio0c->add(15);
	stonewallUnitRatio0c->add(16);
	stonewallUnitRatio0c->add(17);
	stonewallUnitRatio0c->add(18);
	stonewallUnitRatio0c->add(19);
	stonewallUnitRatio0c->add(20);
	stonewallUnitRatio0c->setNth(globalContainer->settings.innUnit1c);
	stonewallUnitRatio0c->visible=false;
	addWidget(stonewallUnitRatio0c);
	
	stonewallUnitText0c=new Text(500, 100, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Wall"));
	addWidget(stonewallUnitText0c);
	stonewallUnitText0c->visible=false;
	
	marketUnitRatio0c=new Number(500, 170, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
	marketUnitRatio0c->add(0);
	marketUnitRatio0c->add(1);
	marketUnitRatio0c->add(2);
	marketUnitRatio0c->add(3);
	marketUnitRatio0c->add(4);
	marketUnitRatio0c->add(5);
	marketUnitRatio0c->add(6);
	marketUnitRatio0c->add(7);
	marketUnitRatio0c->add(8);
	marketUnitRatio0c->add(9);
	marketUnitRatio0c->add(10);
	marketUnitRatio0c->add(11);
	marketUnitRatio0c->add(12);
	marketUnitRatio0c->add(13);
	marketUnitRatio0c->add(14);
	marketUnitRatio0c->add(15);
	marketUnitRatio0c->add(16);
	marketUnitRatio0c->add(17);
	marketUnitRatio0c->add(18);
	marketUnitRatio0c->add(19);
	marketUnitRatio0c->add(20);
	marketUnitRatio0c->setNth(globalContainer->settings.innUnit1c);
	marketUnitRatio0c->visible=false;
	addWidget(marketUnitRatio0c);
	
	marketUnitText0c=new Text(500, 150, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Build Market"));
	addWidget(marketUnitText0c);
	marketUnitText0c->visible=false;
	
	warflagUnitRatio=new Number(500, 220, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
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
	
	warflagUnitText=new Text(500, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("War Flag"));
	addWidget(warflagUnitText);
	warflagUnitText->visible=false;


	clearflagUnitRatio=new Number(500, 270, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
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
	
	clearflagUnitText=new Text(500, 250, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Clearing Flag"));
	addWidget(clearflagUnitText);
	clearflagUnitText->visible=false;

	
	exploreflagUnitRatio=new Number(500, 320, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 20, "menu");
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
	
	exploreflagUnitText=new Text(500, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("Exploration Flag"));
	addWidget(exploreflagUnitText);
	exploreflagUnitText->visible=false;
	
	//shortcuts part
	game_shortcuts=new TextButton( 20, 60, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[game shortcuts]"), GAMESHORTCUTS);
	game_shortcuts->visible=false;

	editor_shortcuts=new TextButton( 230, 60, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[editor shortcuts]"), EDITORSHORTCUTS);
	editor_shortcuts->visible=false;
	
	keyboard_shortcut_names=new List(20, 110, 325, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	keyboard_shortcut_names->visible=false;

	keyboard_shortcuts=new List(355, 110, 275, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	keyboard_shortcuts->visible=false;

	editor_keyboard_shortcuts=new List(355, 110, 275, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	editor_keyboard_shortcuts->visible=false;

	restore_default_shortcuts = new TextButton(20, 320, 610, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[restore default shortcuts]"), RESTOREDEFAULTSHORTCUTS);
	restore_default_shortcuts->visible=false;


	addWidget(game_shortcuts);
	addWidget(editor_shortcuts);
	addWidget(keyboard_shortcut_names);
	addWidget(keyboard_shortcuts);
	addWidget(editor_keyboard_shortcuts);
	addWidget(restore_default_shortcuts);

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

	for(unsigned int x=0; x<shortcut_names.size(); ++x)
	{
		keyboard_shortcuts->addText(shortcut_names[x].c_str());
	}
	for(unsigned int x=0; x<editor_shortcut_names.size(); ++x)
	{
		editor_keyboard_shortcuts->addText(editor_shortcut_names[x].c_str());
	}


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
			
			swarmUnitRatio0c->visible=false;
			swarmUnitRatio0->visible=false;
			innUnitRatio0c->visible=false;
			innUnitRatio0->visible=false;
			innUnitRatio1c->visible=false;
			innUnitRatio1->visible=false;
			innUnitRatio2c->visible=false;
			innUnitRatio2->visible=false;
			hospitalUnitRatio0c->visible=false;
			hospitalUnitRatio1c->visible=false;
			hospitalUnitRatio2c->visible=false;
			racetrackUnitRatio0c->visible=false;
			racetrackUnitRatio1c->visible=false;
			racetrackUnitRatio2c->visible=false;
			swimmingpoolUnitRatio0c->visible=false;
			swimmingpoolUnitRatio1c->visible=false;
			swimmingpoolUnitRatio2c->visible=false;
			barracksUnitRatio0c->visible=false;
			barracksUnitRatio1c->visible=false;
			barracksUnitRatio2c->visible=false;
			schoolUnitRatio0c->visible=false;
			schoolUnitRatio1c->visible=false;
			schoolUnitRatio2c->visible=false;
			defencetowerUnitRatio0c->visible=false;
			defencetowerUnitRatio0->visible=false;
			defencetowerUnitRatio1c->visible=false;
			defencetowerUnitRatio1->visible=false;
			defencetowerUnitRatio2c->visible=false;
			defencetowerUnitRatio2->visible=false;
			stonewallUnitRatio0c->visible=false;
			marketUnitRatio0c->visible=false;
			warflagUnitRatio->visible=false;
			warflagUnitText->visible=false;
			clearflagUnitRatio->visible=false;
			clearflagUnitText->visible=false;
			exploreflagUnitRatio->visible=false;
			exploreflagUnitText->visible=false;
			swarmUnitText0c->visible=false;
			swarmUnitText0->visible=false;
			innUnitText0c->visible=false;
			innUnitText0->visible=false;
			innUnitText1c->visible=false;
			innUnitText1->visible=false;
			innUnitText2c->visible=false;
			innUnitText2->visible=false;
			hospitalUnitText0c->visible=false;
			hospitalUnitText1c->visible=false;
			hospitalUnitText2c->visible=false;
			racetrackUnitText0c->visible=false;
			racetrackUnitText1c->visible=false;
			racetrackUnitText2c->visible=false;
			swimmingpoolUnitText0c->visible=false;
			swimmingpoolUnitText1c->visible=false;
			swimmingpoolUnitText2c->visible=false;
			barracksUnitText0c->visible=false;
			barracksUnitText1c->visible=false;
			barracksUnitText2c->visible=false;
			schoolUnitText0c->visible=false;
			schoolUnitText1c->visible=false;
			schoolUnitText2c->visible=false;
			defencetowerUnitText0c->visible=false;
			defencetowerUnitText0->visible=false;
			defencetowerUnitText1c->visible=false;
			defencetowerUnitText1->visible=false;
			defencetowerUnitText2c->visible=false;
			defencetowerUnitText2->visible=false;
			stonewallUnitText0c->visible=false;
			marketUnitText0c->visible=false;

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

			swarmUnitRatio0c->visible=true;
			swarmUnitRatio0->visible=true;
			innUnitRatio0c->visible=true;
			innUnitRatio0->visible=true;
			innUnitRatio1c->visible=true;
			innUnitRatio1->visible=true;
			innUnitRatio2c->visible=true;
			innUnitRatio2->visible=true;
			hospitalUnitRatio0c->visible=true;
			hospitalUnitRatio1c->visible=true;
			hospitalUnitRatio2c->visible=true;
			racetrackUnitRatio0c->visible=true;
			racetrackUnitRatio1c->visible=true;
			racetrackUnitRatio2c->visible=true;
			swimmingpoolUnitRatio0c->visible=true;
			swimmingpoolUnitRatio1c->visible=true;
			swimmingpoolUnitRatio2c->visible=true;
			barracksUnitRatio0c->visible=true;
			barracksUnitRatio1c->visible=true;
			barracksUnitRatio2c->visible=true;
			schoolUnitRatio0c->visible=true;
			schoolUnitRatio1c->visible=true;
			schoolUnitRatio2c->visible=true;
			defencetowerUnitRatio0c->visible=true;
			defencetowerUnitRatio0->visible=true;
			defencetowerUnitRatio1c->visible=true;
			defencetowerUnitRatio1->visible=true;
			defencetowerUnitRatio2c->visible=true;
			defencetowerUnitRatio2->visible=true;
			stonewallUnitRatio0c->visible=true;
			marketUnitRatio0c->visible=true;
			warflagUnitRatio->visible=true;
			warflagUnitText->visible=true;
			clearflagUnitRatio->visible=true;
			clearflagUnitText->visible=true;
			exploreflagUnitRatio->visible=true;
			exploreflagUnitText->visible=true;
			swarmUnitText0c->visible=true;
			swarmUnitText0->visible=true;
			innUnitText0c->visible=true;
			innUnitText0->visible=true;
			innUnitText1c->visible=true;
			innUnitText1->visible=true;
			innUnitText2c->visible=true;
			innUnitText2->visible=true;
			hospitalUnitText0c->visible=true;
			hospitalUnitText1c->visible=true;
			hospitalUnitText2c->visible=true;
			racetrackUnitText0c->visible=true;
			racetrackUnitText1c->visible=true;
			racetrackUnitText2c->visible=true;
			swimmingpoolUnitText0c->visible=true;
			swimmingpoolUnitText1c->visible=true;
			swimmingpoolUnitText2c->visible=true;
			barracksUnitText0c->visible=true;
			barracksUnitText1c->visible=true;
			barracksUnitText2c->visible=true;
			schoolUnitText0c->visible=true;
			schoolUnitText1c->visible=true;
			schoolUnitText2c->visible=true;
			defencetowerUnitText0c->visible=true;
			defencetowerUnitText0->visible=true;
			defencetowerUnitText1c->visible=true;
			defencetowerUnitText1->visible=true;
			defencetowerUnitText2c->visible=true;
			defencetowerUnitText2->visible=true;
			stonewallUnitText0c->visible=true;
			marketUnitText0c->visible=true;

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
			
			swarmUnitRatio0c->visible=false;
			swarmUnitRatio0->visible=false;
			innUnitRatio0c->visible=false;
			innUnitRatio0->visible=false;
			innUnitRatio1c->visible=false;
			innUnitRatio1->visible=false;
			innUnitRatio2c->visible=false;
			innUnitRatio2->visible=false;
			hospitalUnitRatio0c->visible=false;
			hospitalUnitRatio1c->visible=false;
			hospitalUnitRatio2c->visible=false;
			racetrackUnitRatio0c->visible=false;
			racetrackUnitRatio1c->visible=false;
			racetrackUnitRatio2c->visible=false;
			swimmingpoolUnitRatio0c->visible=false;
			swimmingpoolUnitRatio1c->visible=false;
			swimmingpoolUnitRatio2c->visible=false;
			barracksUnitRatio0c->visible=false;
			barracksUnitRatio1c->visible=false;
			barracksUnitRatio2c->visible=false;
			schoolUnitRatio0c->visible=false;
			schoolUnitRatio1c->visible=false;
			schoolUnitRatio2c->visible=false;
			defencetowerUnitRatio0c->visible=false;
			defencetowerUnitRatio0->visible=false;
			defencetowerUnitRatio1c->visible=false;
			defencetowerUnitRatio1->visible=false;
			defencetowerUnitRatio2c->visible=false;
			defencetowerUnitRatio2->visible=false;
			stonewallUnitRatio0c->visible=false;
			marketUnitRatio0c->visible=false;
			warflagUnitRatio->visible=false;
			warflagUnitText->visible=false;
			clearflagUnitRatio->visible=false;
			clearflagUnitText->visible=false;
			exploreflagUnitRatio->visible=false;
			exploreflagUnitText->visible=false;
			swarmUnitText0c->visible=false;
			swarmUnitText0->visible=false;
			innUnitText0c->visible=false;
			innUnitText0->visible=false;
			innUnitText1c->visible=false;
			innUnitText1->visible=false;
			innUnitText2c->visible=false;
			innUnitText2->visible=false;
			hospitalUnitText0c->visible=false;
			hospitalUnitText1c->visible=false;
			hospitalUnitText2c->visible=false;
			racetrackUnitText0c->visible=false;
			racetrackUnitText1c->visible=false;
			racetrackUnitText2c->visible=false;
			swimmingpoolUnitText0c->visible=false;
			swimmingpoolUnitText1c->visible=false;
			swimmingpoolUnitText2c->visible=false;
			barracksUnitText0c->visible=false;
			barracksUnitText1c->visible=false;
			barracksUnitText2c->visible=false;
			schoolUnitText0c->visible=false;
			schoolUnitText1c->visible=false;
			schoolUnitText2c->visible=false;
			defencetowerUnitText0c->visible=false;
			defencetowerUnitText0->visible=false;
			defencetowerUnitText1c->visible=false;
			defencetowerUnitText1->visible=false;
			defencetowerUnitText2c->visible=false;
			defencetowerUnitText2->visible=false;
			stonewallUnitText0c->visible=false;
			marketUnitText0c->visible=false;

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
		if (swarmUnitRatio0c->getNth() == 0)
		{
			swarmUnitRatio0c->setNth(1);
		}
		globalContainer->settings.swarmUnit0c=swarmUnitRatio0c->getNth();
		
		if (swarmUnitRatio0->getNth() == 0)
		{
			swarmUnitRatio0->setNth(1);
		}
		globalContainer->settings.swarmUnit0=swarmUnitRatio0->getNth();
		
		if (swarmUnitRatio0->getNth() == 0)
		{
			swarmUnitRatio0c->setNth(1);
		}
		globalContainer->settings.swarmUnit0c=swarmUnitRatio0c->getNth();
		
		if (innUnitRatio0c->getNth() == 0)
		{
			innUnitRatio0c->setNth(1);
		}
		globalContainer->settings.innUnit0c=innUnitRatio0c->getNth();
		
		if (innUnitRatio0->getNth() == 0)
		{
			innUnitRatio0->setNth(1);
		}
		globalContainer->settings.innUnit0=innUnitRatio0->getNth();
		
		if (innUnitRatio1c->getNth() == 0)
		{
			innUnitRatio1c->setNth(1);
		}
		globalContainer->settings.innUnit1c=innUnitRatio1c->getNth();
		
		if (innUnitRatio1->getNth() == 0)
		{
			innUnitRatio1->setNth(1);
		}
		globalContainer->settings.innUnit1=innUnitRatio1->getNth();
		
		if (innUnitRatio2c->getNth() == 0)
		{
			innUnitRatio2c->setNth(1);
		}
		globalContainer->settings.innUnit2c=innUnitRatio2c->getNth();
		
		if (innUnitRatio2->getNth() == 0)
		{
			innUnitRatio2->setNth(1);
		}
		globalContainer->settings.innUnit2=innUnitRatio2->getNth();
		
		if (hospitalUnitRatio0c->getNth() == 0)
		{
			hospitalUnitRatio0c->setNth(1);
		}
		globalContainer->settings.hospitalUnit0c=hospitalUnitRatio0c->getNth();
		
		if (hospitalUnitRatio1c->getNth() == 0)
		{
			hospitalUnitRatio1c->setNth(1);
		}
		globalContainer->settings.hospitalUnit1c=hospitalUnitRatio1c->getNth();
		
		if (hospitalUnitRatio2c->getNth() == 0)
		{
			hospitalUnitRatio2c->setNth(1);
		}
		globalContainer->settings.hospitalUnit2c=hospitalUnitRatio2c->getNth();
		
		if (racetrackUnitRatio0c->getNth() == 0)
		{
			racetrackUnitRatio0c->setNth(1);
		}
		globalContainer->settings.racetrackUnit0c=racetrackUnitRatio0c->getNth();
		
		if (racetrackUnitRatio1c->getNth() == 0)
		{
			racetrackUnitRatio1c->setNth(1);
		}
		globalContainer->settings.racetrackUnit1c=racetrackUnitRatio1c->getNth();
		
		if (racetrackUnitRatio2c->getNth() == 0)
		{
			racetrackUnitRatio2c->setNth(1);
		}
		globalContainer->settings.racetrackUnit2c=racetrackUnitRatio2c->getNth();
		
		if (swimmingpoolUnitRatio0c->getNth() == 0)
		{
			swimmingpoolUnitRatio0c->setNth(1);
		}
		globalContainer->settings.swimmingpoolUnit0c=swimmingpoolUnitRatio0c->getNth();
		
		if (swimmingpoolUnitRatio1c->getNth() == 0)
		{
			swimmingpoolUnitRatio1c->setNth(1);
		}
		globalContainer->settings.swimmingpoolUnit1c=swimmingpoolUnitRatio1c->getNth();
		
		if (swimmingpoolUnitRatio2c->getNth() == 0)
		{
			swimmingpoolUnitRatio2c->setNth(1);
		}
		globalContainer->settings.swimmingpoolUnit2c=swimmingpoolUnitRatio2c->getNth();
		
		if (barracksUnitRatio0c->getNth() == 0)
		{
			barracksUnitRatio0c->setNth(1);
		}
		globalContainer->settings.barracksUnit0c=barracksUnitRatio0c->getNth();
		
		if (barracksUnitRatio1c->getNth() == 0)
		{
			barracksUnitRatio1c->setNth(1);
		}
		globalContainer->settings.barracksUnit1c=barracksUnitRatio1c->getNth();
		
		if (barracksUnitRatio2c->getNth() == 0)
		{
			barracksUnitRatio2c->setNth(1);
		}
		globalContainer->settings.barracksUnit2c=barracksUnitRatio2c->getNth();
					
		if (schoolUnitRatio0c->getNth() == 0)
		{
			schoolUnitRatio0c->setNth(1);
		}
		globalContainer->settings.schoolUnit0c=schoolUnitRatio0c->getNth();
		
		if (schoolUnitRatio1c->getNth() == 0)
		{
			schoolUnitRatio1c->setNth(1);
		}
		globalContainer->settings.schoolUnit1c=schoolUnitRatio1c->getNth();
		
		if (schoolUnitRatio2c->getNth() == 0)
		{
			schoolUnitRatio2c->setNth(1);
		}
		globalContainer->settings.schoolUnit2c=schoolUnitRatio2c->getNth();
		
		if (defencetowerUnitRatio0c->getNth() == 0)
		{
			defencetowerUnitRatio0c->setNth(1);
		}
		globalContainer->settings.defencetowerUnit0c=defencetowerUnitRatio0c->getNth();
		
		if (defencetowerUnitRatio0->getNth() == 0)
		{
			defencetowerUnitRatio0->setNth(1);
		}
		globalContainer->settings.defencetowerUnit0=defencetowerUnitRatio0->getNth();
		
		if (defencetowerUnitRatio1c->getNth() == 0)
		{
			defencetowerUnitRatio1c->setNth(1);
		}
		globalContainer->settings.defencetowerUnit1c=defencetowerUnitRatio1c->getNth();
		
		if (defencetowerUnitRatio1->getNth() == 0)
		{
			defencetowerUnitRatio1->setNth(1);
		}
		globalContainer->settings.defencetowerUnit1=defencetowerUnitRatio1->getNth();
		
		if (defencetowerUnitRatio2c->getNth() == 0)
		{
			defencetowerUnitRatio2c->setNth(1);
		}
		globalContainer->settings.defencetowerUnit2c=defencetowerUnitRatio2c->getNth();
		
		if (defencetowerUnitRatio2->getNth() == 0)
		{
			defencetowerUnitRatio2->setNth(1);
		}
		globalContainer->settings.defencetowerUnit2=defencetowerUnitRatio2->getNth();
		
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
		
		if (stonewallUnitRatio0c->getNth() == 0)
		{
			stonewallUnitRatio0c->setNth(1);
		}
		globalContainer->settings.stonewallUnit0c=stonewallUnitRatio0c->getNth();
		
		if (marketUnitRatio0c->getNth() == 0)
		{
			marketUnitRatio0c->setNth(1);
		}
		globalContainer->settings.marketUnit0c=marketUnitRatio0c->getNth();
		
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
