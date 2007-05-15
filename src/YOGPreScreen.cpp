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

#include <GUIText.h>
#include <GUITextInput.h>
#include <GUITextArea.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <GUIAnimation.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <GraphicContext.h>

#include "GlobalContainer.h"
#include "Settings.h"
#include "YOGScreen.h"
#include "YOGPreScreen.h"

YOGPreScreen::YOGPreScreen()
{
	addWidget(new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27));
	addWidget(new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[login]"), LOGIN, 13));
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[yog]")));

	addWidget(new Text(20, 260, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Enter your nickname :]")));
	login=new TextInput(20, 290, 300, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", globalContainer->getUsername(), false, 32);
	addWidget(login);
	
	addWidget(new Text(20, 330, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Enter your password :]")));
	password=new TextInput(20, 360, 300, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", globalContainer->settings.password.c_str(), true, 32, true);
	addWidget(password);
	
	newYogPassword=new OnOffButton(20, 400, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, false, NEW_USER);
	newYogPasswordText=new Text(47, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard",
		Toolkit::getStringTable()->getString("[Register a new YOG user with password]"));
	addWidget(newYogPassword);
	addWidget(newYogPasswordText);
	
	rememberYogPassword=new OnOffButton(20, 440, 21, 21, ALIGN_SCREEN_CENTERED,ALIGN_SCREEN_CENTERED, password->getText().length() > 0, NEW_USER);
	rememberYogPasswordText=new Text(47, 440, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard",
		Toolkit::getStringTable()->getString("[Remember YOG password localy]"));
	addWidget(rememberYogPassword);
	addWidget(rememberYogPasswordText);
	
	
	statusText=new TextArea(20, 130, 600, 120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	addWidget(statusText);
	
	animation=new Animation(32, 90, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "data/gfx/rotatingEarth", 0, 20, 2);
	animation->visible=false;
	addWidget(animation);
	
	oldYOGExternalStatusState=YOG::YESTS_BAD;
	endExecutionValue=EXECUTING;
	connectOnNextTimer=false;
}

YOGPreScreen::~YOGPreScreen()
{
	Toolkit::releaseSprite("data/gfx/rotatingEarth");
	globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
}

void YOGPreScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CANCEL)
		{
			yog->deconnect();
			endExecutionValue=CANCEL;
		}
		else if (par1==LOGIN)
		{
			char *s=yog->getStatusString(YOG::YESTS_CONNECTING);//This is a small "hack" to looks more user-friendly.
			statusText->setText(s);
			delete[] s;
			oldYOGExternalStatusState=YOG::YESTS_CONNECTING;
			connectOnNextTimer=true;
			animation->show();
			globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_WAIT);
		}
		else if (par1==-1)
		{
			yog->deconnect();
			endExecutionValue=-1;
		}
	}
	if (action==TEXT_ACTIVATED)
	{
		if (source==login)
			password->deactivate();
		else if (source==password)
			login->deactivate();
		else
		{
			assert(false);
			login->deactivate();
			password->deactivate();
		}
	}
}

void YOGPreScreen::onTimer(Uint32 tick)
{
	yog->step();
	if (endExecutionValue!=EXECUTING)
	{
		if (yog->yogGlobalState<=YOG::YGS_NOT_CONNECTING)
			endExecute(endExecutionValue);
	}
	else if (yog->yogGlobalState>=YOG::YGS_CONNECTED)
	{
		if (rememberYogPassword->getState())
		{
			globalContainer->settings.password.assign(password->getText(), 0, 32);
			globalContainer->settings.save();
		}
		animation->hide();
		globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
		if (verbose)
			printf("YOGPreScreen:: starting YOGScreen...\n");
		YOGScreen yogScreen;
		int yogReturnCode=yogScreen.execute(globalContainer->gfx, 40);
		if (yogReturnCode==YOGScreen::CANCEL)
		{
			yog->deconnect();
		}
		else if (yogReturnCode==-1)
		{
			yog->deconnect();
			endExecutionValue=-1;
		}
		else
			assert(false);
		if (verbose)
			printf("YOGPreScreen:: YOGScreen has ended ...\n");
	}
	if (connectOnNextTimer)
	{
		yog->enableConnection(login->getText().c_str(), password->getText().c_str(), newYogPassword->getState());
		connectOnNextTimer=false;
	}
	else if (yog->externalStatusState!=oldYOGExternalStatusState)
	{
		if (yog->externalStatusState!=YOG::YESTS_CONNECTING)
		{
			animation->hide();
			globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
		}
		char *s=yog->getStatusString();
		statusText->setText(s);
		delete[] s;
		oldYOGExternalStatusState=yog->externalStatusState;
	}

}
