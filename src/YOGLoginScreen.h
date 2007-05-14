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

#ifndef __YOGLoginScreen_h
#define __YOGLoginScreen_h

#include "Glob2Screen.h"
#include "YOGClient.h"
#include "YOGScreen.h"

namespace GAGGUI
{
	class OnOffButton;
	class Text;
	class TextInput;
	class TextArea;
	class Animation;
}

///This handles with connecting the user to YOG and logging them in.
///This assumes the client has not yet connected with YOG
class YOGLoginScreen : public Glob2Screen
{
public:
	///Construct with the given YOG client.
	///The provided client should not yet be connected to YOG.
	YOGLoginScreen(boost::shared_ptr<YOGClient> client);
	virtual ~YOGLoginScreen();

	enum
	{
		EXECUTING=0,
		LOGIN=1,
		CANCEL=2,
		NEW_USER=10
	};

private:
	enum
	{
		WAITING=1,
		STARTED=2
	};

	virtual void onTimer(Uint32 tick);
	void onAction(Widget *source, Action action, int par1, int par2);

	void attemptLogin();
	void attemptRegistration();

	TextInput *login, *password;
	OnOffButton *newYogPassword, *rememberYogPassword;
	Text *newYogPasswordText, *rememberYogPasswordText;
	TextArea *statusText;
	Animation *animation;
	
	YOGClient::ConnectionState oldConnectionState;
	
	boost::shared_ptr<YOGClient> client;
};

#endif
