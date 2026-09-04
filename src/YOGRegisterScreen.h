// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

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
	YOGRegisterScreen(std::shared_ptr<YOGClient> client);
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
	void handleYOGClientEvent(std::shared_ptr<YOGClientEvent> event);


	///Attempt a registration with the entered information
	void attemptRegistration();

	TextArea *statusText;
	TextInput *login, *password, *passwordRepeat;
	Animation *animation;
	bool wasConnecting;
	bool changeTabAgain;
	

	std::shared_ptr<YOGClient> client;
};

#endif
