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
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>
#include "SoundMixer.h"
#include <ostream>

SettingsScreen::SettingsScreen()
{
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
	musicVol=new Selector(230, 420, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 180, 8, globalContainer->settings.musicVolume, 1);
	addWidget(musicVol);
	musicVolText=new Text(230, 390, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Music volume]"), 300);
	addWidget(musicVolText);
	setVisibilityFromAudioSettings();
	
	// Screen entry/quit part
	ok=new TextButton( 440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	addWidget(ok);
	cancel=new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, NULL, -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	addWidget(cancel);
	title=new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[settings]"));
	addWidget(title);

	oldLanguage = Toolkit::getStringTable()->getLang();
	oldScreenW = globalContainer->settings.screenWidth;
	oldScreenH = globalContainer->settings.screenHeight;
	oldScreenFlags = globalContainer->settings.screenFlags;
	oldOptionFlags = globalContainer->settings.optionFlags;
	oldMusicVol = globalContainer->settings.musicVolume;
	oldMute = globalContainer->settings.mute;

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

			endExecute(par1);
		}
	}
	else if (action==LIST_ELEMENT_SELECTED)
	{
		if (source==languageList)
		{
			Toolkit::getStringTable()->setLang(par1);
			ok->setText(Toolkit::getStringTable()->getString("[ok]"));
			cancel->setText(Toolkit::getStringTable()->getString("[Cancel]"));

			title->setText(Toolkit::getStringTable()->getString("[settings]"));
			language->setText(Toolkit::getStringTable()->getString("[language-tr]"));
			display->setText(Toolkit::getStringTable()->getString("[display]"));
			usernameText->setText(Toolkit::getStringTable()->getString("[username]"));
			audio->setText(Toolkit::getStringTable()->getString("[audio]"));

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
	}
	else if (action==VALUE_CHANGED)
	{
		globalContainer->settings.musicVolume = musicVol->getValue();
		globalContainer->mix->setVolume(globalContainer->settings.musicVolume, globalContainer->settings.mute);
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
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
