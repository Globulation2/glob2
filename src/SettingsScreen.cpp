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

SettingsScreen::SettingsScreen()
{
	// language part
	language=new Text(20, 60, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[language-tr]"));
	addWidget(language);
	languageList=new List(20, 90, 160, 160, ALIGN_LEFT, ALIGN_TOP, "standard");
	for (int i=0; i<Toolkit::getStringTable()->getNumberOfLanguage(); i++)
		languageList->addText(Toolkit::getStringTable()->getStringInLang("[language]", i));
	addWidget(languageList);

	// graphics part
	display=new Text(245, 60, ALIGN_RIGHT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[display]"));
	addWidget(display);
	modeList=new List(245, 90, 100, 160, ALIGN_RIGHT, ALIGN_TOP, "standard");
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
	
	rendererList = new List(175, 90, 50, 45, ALIGN_RIGHT, ALIGN_TOP, "standard");
	rendererList->addText("SDL");
	rendererList->addText("GL");
	addWidget(rendererList);
	
	depthList = new List(110, 90, 50, 45, ALIGN_RIGHT, ALIGN_TOP, "standard");
	depthList->addText("32");
	depthList->addText("16");
	addWidget(depthList);

	fullscreen=new OnOffButton(200, 90+60, 25, 25, ALIGN_RIGHT, ALIGN_TOP, globalContainer->settings.screenFlags&DrawableSurface::FULLSCREEN, FULLSCREEN);
	addWidget(fullscreen);
	fullscreenText=new Text(20, 90+60, ALIGN_RIGHT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[fullscreen]"), 160);
	addWidget(fullscreenText);
	
	lowquality=new OnOffButton(200, 120+60, 25, 25, ALIGN_RIGHT, ALIGN_TOP, globalContainer->settings.optionFlags&GlobalContainer::OPTION_LOW_SPEED_GFX, LOWQUALITY);
	addWidget(lowquality);
	lowqualityText=new Text(20, 120+60, ALIGN_RIGHT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[lowquality]"), 160);
	addWidget(lowqualityText);

	hwaccel=new OnOffButton(200, 180+60, 25, 25, ALIGN_RIGHT, ALIGN_TOP, globalContainer->settings.screenFlags&DrawableSurface::HWACCELERATED, HWACCLEL);
	addWidget(hwaccel);
	hwaccelText=new Text(20, 180+60, ALIGN_RIGHT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[hwaccel]"), 160);
	addWidget(hwaccelText);

	dblbuff=new OnOffButton(200, 150+60, 25, 25, ALIGN_RIGHT, ALIGN_TOP, globalContainer->settings.screenFlags&DrawableSurface::DOUBLEBUF, DBLBUFF);
	addWidget(dblbuff);
	dblbuffText=new Text(20, 150+60, ALIGN_RIGHT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[dblbuff]"), 160);
	addWidget(dblbuffText);
	
	setVisibilityFromGraphicType();

	// Username part
	userName=new TextInput(20, 80, 160, 25, ALIGN_LEFT, ALIGN_BOTTOM, "standard", globalContainer->getUsername(), true, 32);
	addWidget(userName);
	usernameText=new Text(20, 130, ALIGN_LEFT, ALIGN_BOTTOM, "standard", Toolkit::getStringTable()->getString("[username]"));
	addWidget(usernameText);

	// Audio part
	audio=new Text(245, 130, ALIGN_RIGHT, ALIGN_BOTTOM, "standard", Toolkit::getStringTable()->getString("[audio]"));
	addWidget(audio);
	musicVol=new Selector(20, 80, ALIGN_RIGHT, ALIGN_BOTTOM, 256, 8, globalContainer->settings.musicVolume, 1);
	addWidget(musicVol);
	musicVolText=new Text(200, 100, ALIGN_RIGHT, ALIGN_BOTTOM, "standard", Toolkit::getStringTable()->getString("[Music volume]"));
	addWidget(musicVolText);

	// Screen entry/quit part
	ok=new TextButton( 60, 20, 200, 40, ALIGN_LEFT, ALIGN_BOTTOM, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	addWidget(ok);
	cancel=new TextButton(60, 20, 200, 40, ALIGN_RIGHT, ALIGN_BOTTOM, NULL, -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	addWidget(cancel);
	title=new Text(0, 18, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[settings]"));
	addWidget(title);

	oldLanguage = Toolkit::getStringTable()->getLang();
	oldScreenW = globalContainer->settings.screenWidth;
	oldScreenH = globalContainer->settings.screenHeight;
	oldScreenDepth = globalContainer->settings.screenDepth;
	oldScreenFlags = globalContainer->settings.screenFlags;
	oldGraphicType = globalContainer->settings.graphicType;
	oldOptionFlags = globalContainer->settings.optionFlags;

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

			globalContainer->settings.musicVolume = oldMusicVol;
			globalContainer->mix->setVolume(globalContainer->settings.musicVolume);

			globalContainer->settings.screenWidth = oldScreenW;
			globalContainer->settings.screenHeight = oldScreenH;
			globalContainer->settings.screenDepth = oldScreenDepth;
			globalContainer->settings.screenFlags = oldScreenFlags;
			globalContainer->settings.graphicType = oldGraphicType;
			if (gfxAltered)
				updateGfxCtx();

			globalContainer->settings.optionFlags = oldOptionFlags;

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
			hwaccelText->setText(Toolkit::getStringTable()->getString("[hwaccel]"));
			dblbuffText->setText(Toolkit::getStringTable()->getString("[dblbuff]"));
			lowqualityText->setText(Toolkit::getStringTable()->getString("[lowquality]"));

			musicVolText->setText(Toolkit::getStringTable()->getString("[Music volume]"));
		}
		else if (source==modeList)
		{
			int w, h;
			sscanf(modeList->getText(par1), "%dx%d", &w, &h);
			globalContainer->settings.screenWidth=w;
			globalContainer->settings.screenHeight=h;
			updateGfxCtx();
		}
		else if (source==rendererList)
		{
			globalContainer->settings.graphicType = par1;
			updateGfxCtx();
		}
		else if (source==depthList)
		{
			globalContainer->settings.screenDepth = atoi(depthList->getText(par1));
			updateGfxCtx();
		}
	}
	else if (action==VALUE_CHANGED)
	{
		globalContainer->settings.musicVolume=musicVol->getValue();
		globalContainer->mix->setVolume(globalContainer->settings.musicVolume);
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
				globalContainer->settings.screenFlags|=DrawableSurface::FULLSCREEN;
				globalContainer->settings.screenFlags&=~(DrawableSurface::RESIZABLE);
			}
			else
			{
				globalContainer->settings.screenFlags&=~(DrawableSurface::FULLSCREEN);
				globalContainer->settings.screenFlags|=DrawableSurface::RESIZABLE;
			}
			updateGfxCtx();
		}
		else if ((source==hwaccel) && (globalContainer->settings.graphicType != DrawableSurface::GC_GL))
		{
			if (hwaccel->getState())
			{
				globalContainer->settings.screenFlags|=DrawableSurface::HWACCELERATED;
			}
			else
			{
				globalContainer->settings.screenFlags&=~(DrawableSurface::HWACCELERATED);
			}
			updateGfxCtx();
		}
		else if ((source==dblbuff) && (globalContainer->settings.graphicType != DrawableSurface::GC_GL))
		{
			if (dblbuff->getState())
			{
				globalContainer->settings.screenFlags|=DrawableSurface::DOUBLEBUF;
			}
			else
			{
				globalContainer->settings.screenFlags&=~(DrawableSurface::DOUBLEBUF);
			}
			updateGfxCtx();
		}
	}
}

void SettingsScreen::setVisibilityFromGraphicType(void)
{
	dblbuff->visible = globalContainer->settings.graphicType != DrawableSurface::GC_GL;
	dblbuffText->visible = globalContainer->settings.graphicType != DrawableSurface::GC_GL;
	hwaccel->visible = globalContainer->settings.graphicType != DrawableSurface::GC_GL;
	hwaccelText->visible = globalContainer->settings.graphicType != DrawableSurface::GC_GL;
}

void SettingsScreen::updateGfxCtx(void)
{
	globalContainer->gfx->setRes(globalContainer->settings.screenWidth, globalContainer->settings.screenHeight, globalContainer->settings.screenDepth, globalContainer->settings.screenFlags, (DrawableSurface::GraphicContextType)globalContainer->settings.graphicType);
	setVisibilityFromGraphicType();
	dispatchPaint(globalContainer->gfx);
	addUpdateRect(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH());
	gfxAltered = true;
}

int SettingsScreen::menu(void)
{
	return SettingsScreen().execute(globalContainer->gfx, 30);
}
