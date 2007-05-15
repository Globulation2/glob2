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

#ifndef __YOG_PRE_SCREEN_H
#define __YOG_PRE_SCREEN_H

#include "Glob2Screen.h"
#include "YOG.h"

namespace GAGGUI
{
	class OnOffButton;
	class Text;
	class TextInput;
	class TextArea;
	class Animation;
}

class YOGPreScreen : public Glob2Screen
{
	static const bool verbose = false;
public:
	enum
	{
		EXECUTING=0,
		LOGIN=1,
		CANCEL=2,
		NEW_USER=10
	};
	enum
	{
		WAITING=1,
		STARTED=2
	};
public:
	YOGPreScreen();
	virtual ~YOGPreScreen();
	virtual void onTimer(Uint32 tick);
	void onAction(Widget *source, Action action, int par1, int par2);
	
	int endExecutionValue;
	TextInput *login, *password;
	OnOffButton *newYogPassword, *rememberYogPassword;
	Text *newYogPasswordText, *rememberYogPasswordText;
	TextArea *statusText;
	Animation *animation;
	YOG::ExternalStatusState oldYOGExternalStatusState;
	
	bool connectOnNextTimer;
};

#endif
