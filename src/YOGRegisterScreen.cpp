/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "YOGRegisterScreen.h"

#include "YOGClientEvent.h"
#include "YOGClient.h"


#include "GUIButton.h"
#include "GUITextArea.h"
#include "GUIText.h"
#include "GUITextInput.h"
#include "GUIAnimation.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "GlobalContainer.h"



YOGRegisterScreen::YOGRegisterScreen(boost::shared_ptr<YOGClient> client)
	: client(client)
{
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Register]")));

	addWidget(new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27));
	addWidget(new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Register]"), REGISTER, 13));

	statusText=new TextArea(20, 130, 600, 120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", true, Toolkit::getStringTable()->getString("[YESTS_CREATED]"));
	addWidget(statusText);

	addWidget(new Text(20, 260, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Enter your nickname :]")));
	login=new TextInput(20, 290, 300, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", globalContainer->settings.getUsername(), true, 32);
	addWidget(login);

	addWidget(new Text(20, 330, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Enter your password :]")));
	password=new TextInput(20, 360, 300, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", false, 32, true);
	addWidget(password);

	addWidget(new Text(20, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Repeat your password :]")));
	passwordRepeat=new TextInput(20, 430, 300, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", false, 32, true);
	addWidget(passwordRepeat);

	animation=new Animation(32, 90, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "data/gfx/rotatingEarth", 0, 20, 2);
	animation->visible=false;
	addWidget(animation);

	wasConnecting=false;
	changeTabAgain=true;

	client->addEventListener(this);
}



YOGRegisterScreen::~YOGRegisterScreen()
{
	client->removeEventListener(this);
}



void YOGRegisterScreen::onTimer(Uint32 tick)
{
	if(wasConnecting && !client->isConnecting())
	{
		if(!client->isConnected())
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_UNABLE_TO_CONNECT]"));
			animation->visible=false;
		}
		wasConnecting = false;
	}

	client->update();
	changeTabAgain=true;
}



void YOGRegisterScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CANCEL)
		{
			endExecute(Cancelled);
		}
		else if (par1==REGISTER)
		{
			if(password->getText() != passwordRepeat->getText())
			{
				statusText->setText(Toolkit::getStringTable()->getString("[YESTS_PASSWORDS_DONT_MATCH]"));
			}
			else if(password->getText() != "")
			{
				//Update the gui
				animation->show();
				globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_WAIT);
				statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTING]"));

				client->connect(YOG_SERVER_IP);
				wasConnecting = true;
			}
		}
	}
	if (action==TEXT_ACTIVATED)
	{
		if (source==login)
		{
			password->deactivate();
			passwordRepeat->deactivate();
		}
		else if (source==password)
		{
			login->deactivate();
			passwordRepeat->deactivate();
		}
		else if (source==passwordRepeat)
		{
			login->deactivate();
			password->deactivate();
		}
	}
	if (action==TEXT_TABBED)
	{
		if (login->isActivated() && changeTabAgain)
		{
			login->deactivate();
			password->activate();
			changeTabAgain=false;
		}
		else if (password->isActivated() && changeTabAgain)
		{
			password->deactivate();
			passwordRepeat->activate();
			changeTabAgain=false;
		}
		else if (passwordRepeat->isActivated() && changeTabAgain)
		{
			passwordRepeat->deactivate();
			login->activate();
			changeTabAgain=false;
		}
	}
}



void YOGRegisterScreen::handleYOGClientEvent(boost::shared_ptr<YOGClientEvent> event)
{
	//std::cout<<"YOGLoginScreen: recieved event "<<event->format()<<std::endl;
	Uint8 type = event->getEventType();
	if(type == YEConnected)
	{
		attemptRegistration();
	}
	else if(type == YEConnectionLost)
	{
		//shared_ptr<YOGConnectionLostEvent> info = static_pointer_cast<YOGConnectionLostEvent>(event);
		animation->visible=false;
		statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_LOST]"));
	}
	else if(type == YELoginAccepted)
	{
		//shared_ptr<YOGLoginAcceptedEvent> info = static_pointer_cast<YOGLoginAcceptedEvent>(event);
		animation->visible=false;

		endExecute(Connected);
	}
	else if(type == YELoginRefused)
	{
		shared_ptr<YOGLoginRefusedEvent> info = static_pointer_cast<YOGLoginRefusedEvent>(event);
		animation->visible=false;
		YOGLoginState reason = info->getReason();
		if(reason == YOGPasswordIncorrect)
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_BAD_PASSWORD]"));
		}
		else if(reason == YOGUsernameAlreadyUsed)
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_ALREADY_PASSWORD]"));
		}
		else if(reason == YOGUserNotRegistered)
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_BAD_PASSWORD_NON_ZERO]"));
		}
		else if(reason == YOGClientVersionTooOld)
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_PROTOCOL_TOO_OLD]"));
		}
		else if(reason == YOGAlreadyAuthenticated)
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_USERNAME_ALLREADY_USED]"));
		}
		else if(reason == YOGUsernameBanned)
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_USERNAME_BANNED]"));
		}
		else if(reason == YOGIPAddressBanned)
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_IP_TEMPORARILY_BANNED]"));
		}
		else if(reason == YOGNameInvalidSpecialCharacters)
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_USERNAME_INVALID_SPECIAL_CHARACTERS]"));
		}
		else if(reason == YOGLoginUnknown)
		{
			statusText->setText(Toolkit::getStringTable()->getString("[YESTS_CONNECTION_REFUSED_UNEXPLAINED]"));
		}
		client->disconnect();
	}

}



void YOGRegisterScreen::attemptRegistration()
{
	//Attempt the registration
	client->attemptRegistration(login->getText(), password->getText());
}



