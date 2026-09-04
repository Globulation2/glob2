// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __YOGLoginScreen_h
#define __YOGLoginScreen_h

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

///This handles with connecting the user to YOG and logging them in.
///This assumes the client has not yet connected with YOG
class YOGLoginScreen : public Glob2Screen, public YOGClientEventListener
{
public:
	///Construct with the given YOG client.
	///The provided client should not yet be connected to YOG.
	YOGLoginScreen(std::shared_ptr<YOGClient> client);
	virtual ~YOGLoginScreen();

	enum
	{
		Cancelled,
		LoggedIn,
		ConnectionLost,
	};

private:
	enum
	{
		EXECUTING=0,
		LOGIN=1,
		CANCEL=2,
		REGISTER=3,
		NEW_USER=10
	};

	enum
	{
		WAITING=1,
		STARTED=2
	};

	void onTimer(Uint32 tick);
	void onAction(Widget *source, Action action, int par1, int par2);
	
	///Responds to YOG events
	void handleYOGClientEvent(std::shared_ptr<YOGClientEvent> event);

	///Attempt a login with the entered information
	void attemptLogin();
	
	///Opens the lobby screen
	void runLobby();

	TextInput *login, *password;
	OnOffButton *rememberYogPassword;
	Text *rememberYogPasswordText;
	TextArea *statusText;
	Animation *animation;
	
	bool wasConnecting;
	
	std::shared_ptr<YOGClient> client;
	bool changeTabAgain;
};

#endif
