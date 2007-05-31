/*
  Copyright (C) 2007 Bradley Arsenault

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
#include "YOGLoginScreen.h"

YOGLoginScreen::YOGLoginScreen(boost::shared_ptr<YOGClient> client)
	: client(client)
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
		
	wasConnected=false;
}

YOGLoginScreen::~YOGLoginScreen()
{
	Toolkit::releaseSprite("data/gfx/rotatingEarth");
	globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
}

void YOGLoginScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CANCEL)
		{
			endExecute(Cancelled);
		}
		else if (par1==LOGIN)
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTING]"));
			client->connect(YOG_SERVER_IP);
			if(client->isConnected())
			{
				wasConnected=true;
				if(newYogPassword->getState())
					attemptRegistration();
				else
					attemptLogin();
			}
			else
			{
				statusText->setText(Toolkit::getStringTable()->getString("[YESTS_UNABLE_TO_CONNECT]"));
			}
		}
	}
	if (action==TEXT_ACTIVATED)
	{
		if (source==login)
			password->deactivate();
		else if (source==password)
			login->deactivate();
	}
}

void YOGLoginScreen::onTimer(Uint32 tick)
{
	//See if the connection is still present
	if(!client->isConnected() && wasConnected)
	{
		statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_LOST]"));
	}
	else
	{
		client->update();
		//If the connection state has changed
		if(client->getConnectionState() != oldConnectionState)
		{
			YOGLoginState login = client->getLoginState();
			if(login == YOGPasswordIncorrect)
			{
				statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_BAD_PASSWORD]"));
			}
			else if(login == YOGUsernameAlreadyUsed)
			{
				statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_USERNAME_ALLREADY_USED]"));
			}
			else if(login == YOGUserNotRegistered)
			{
				statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_BAD_PASSWORD_NON_ZERO]"));
			}
			else if(client->getLoginState() == YOGLoginSuccessful)
			{
				YOGScreen screen(client);
				screen.execute(globalContainer->gfx, 40);
				endExecute(LoggedIn);
			}
			oldConnectionState = client->getConnectionState();
		}
	}
}



void YOGLoginScreen::attemptLogin()
{
	//Save the password
	globalContainer->settings.password.assign(password->getText(), 0, 32);
	globalContainer->settings.save();
	//Update the gui
	animation->show();
	globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_WAIT);
	statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTING]"));
	//Attempt the login
	client->attemptLogin(login->getText(), password->getText());
}



void YOGLoginScreen::attemptRegistration()
{
	//Save the password
	globalContainer->settings.password.assign(password->getText(), 0, 32);
	globalContainer->settings.save();
	//Update the gui
	animation->show();
	globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_WAIT);
	statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTING]"));
	//Attempt the registration
	client->attemptRegistration(login->getText(), password->getText());
}

