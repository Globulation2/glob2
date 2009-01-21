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

#ifndef YOGRegisterScreen_h
#define YOGRegisterScreen_h

#include "Glob2Screen.h"
#include "YOGClientEventListener.h"


namespace GAGGUI
{
	class OnOffButton;
	class Text;
	class TextInput;
	class TextArea;
	class Animation;
}

class YOGClient;

class YOGRegisterScreen : public Glob2Screen, public YOGClientEventListener
{
public:
	///Construct with the given YOG client.
	///The provided client should not yet be connected to YOG.
	YOGRegisterScreen(boost::shared_ptr<YOGClient> client);
	///Destroy the screen
	~YOGRegisterScreen();
	enum
	{
		Cancelled,
		Connected,
	};


private:
	enum
	{
		CANCEL,
		REGISTER,
	};

	void onTimer(Uint32 tick);
	void onAction(Widget *source, Action action, int par1, int par2);
	
	///Responds to YOG events
	void handleYOGClientEvent(boost::shared_ptr<YOGClientEvent> event);


	///Attempt a registration with the entered information
	void attemptRegistration();

	TextArea *statusText;
	TextInput *login, *password, *passwordRepeat;
	Animation *animation;
	bool wasConnecting;
	bool changeTabAgain;
	

	boost::shared_ptr<YOGClient> client;
};

#endif
