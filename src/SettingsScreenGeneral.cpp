// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Construction of the "General Settings" tab: OK / Cancel buttons (which sit
// on every tab), language list, display / video-mode list, graphics toggles
// (fullscreen / OpenGL / lowquality / customcursor / remember-unit /
// scrollwheel) plus the reboot warning, the username field, and the audio
// section. Split out of SettingsScreen.cpp to keep each file under 500 lines.
//
// Event handling for these widgets lives in SettingsScreen.cpp alongside the
// onAction dispatcher and the GfxContext / audio-mute visibility plumbing.

#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include <sstream>
#include <GUIText.h>
#include <GUITextInput.h>
#include <GUIList.h>
#include <GUIButton.h>
#include <GUISelector.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>


void SettingsScreen::buildOkCancelButtons()
{
	ok=new TextButton( 230, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[ok]"), OK);
	addWidget(ok);
	cancel=new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL);
	addWidget(cancel);
}


void SettingsScreen::buildLanguageWidgets()
{
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
}


void SettingsScreen::buildDisplayWidgets()
{
	display=new Text(230, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[display]"));
	addWidgetToGroup(display, generalGroup);
	actDisplay = new Text(440, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", actDisplayModeToString().c_str());
	addWidgetToGroup(actDisplay, generalGroup);
	modeList=new List(440, 90, 180, 190, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	const auto modes = globalContainer->gfx->listVideoModes();
	const int standardResolutionsCount=5;
	int standardResolutions[standardResolutionsCount][2]={{640,480},{800,600},{1024,768},{1280,1024},{1600,1200}};
	for (auto const& mode : modes)
	{
		std::ostringstream ost;
		ost << mode.w << "x" << mode.h;
		if (!modeList->isText(ost.str().c_str()))
			modeList->addText(ost.str().c_str());
	}
	for(int i=0; i<standardResolutionsCount; i++)
	{
		std::ostringstream ost;
		ost << standardResolutions[i][0] << "x" << standardResolutions[i][1];
		if (!modeList->isText(ost.str().c_str()))
		{
			ost << " *";
			modeList->addText(ost.str().c_str());
		}
	}
	addWidgetToGroup(modeList, generalGroup);
	modeListNote=new Text(modeList->getLeft(), modeList->getTop()+modeList->getHeight(), ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[no fullscreen]"));
	addWidgetToGroup(modeListNote, generalGroup);
}


void SettingsScreen::buildGraphicsToggles()
{
	fullscreen=new OnOffButton(230, 90, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.screenFlags & GraphicContext::FULLSCREEN, FULLSCREEN);
	addWidgetToGroup(fullscreen, generalGroup);
	fullscreenText=new Text(260, 90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[fullscreen]"), 180);
	addWidgetToGroup(fullscreenText, generalGroup);

	usegpu=new OnOffButton(230, 90 + 30, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, globalContainer->settings.screenFlags & GraphicContext::USEGPU, USEGL);
	addWidgetToGroup(usegpu, generalGroup);
	usegpuText=new Text(260, 90 + 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[OpenGL]"), 180);
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
	//TODO: warning style should be defined centrally.
	rebootWarning->setStyle(Font::Style(Font::STYLE_BOLD, 255, 60, 60));
	addWidget(rebootWarning);

	setVisibilityFromGraphicType();
	rebootWarning->visible=false;
}


void SettingsScreen::buildUsernameWidgets()
{
	userName=new TextInput(20, 360, 180, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", globalContainer->settings.getUsername(), true, 32);
	addWidgetToGroup(userName, generalGroup);
	usernameText=new Text(20, 330, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[username]"));
	addWidgetToGroup(usernameText, generalGroup);
}


void SettingsScreen::buildAudioWidgets()
{
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
}
