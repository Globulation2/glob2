/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "YOGScreen.h"
#include "YOGPreScreen.h"
#include "GlobalContainer.h"

YOGPreScreen::YOGPreScreen()
{
	addWidget(new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[Cancel]"), CANCEL, 27));
	addWidget(new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[login]"), LOGIN, 13));
	addWidget(new Text(0, 18, globalContainer->menuFont, globalContainer->texts.getString("[yog]"), 640));
	
	login=new TextInput(20, 435, 400, 25, globalContainer->standardFont, globalContainer->userName, true, 32);
	addWidget(login);
	
	statusText=new TextArea(20, 290, 600, 50, globalContainer->standardFont);
	addWidget(statusText);
	oldYOGExternalStatusState=YOG::YESTS_BAD;
	
	endExecutionValue=EXECUTING;
	connectOnNextTimer=false;
}

YOGPreScreen::~YOGPreScreen()
{
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
		}
		else if (par1==-1)
		{
			yog->deconnect();
			endExecutionValue=-1;
		}
		else
			assert(false);
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
		printf("YOGPreScreen:: starting YOGScreen...\n");
		YOGScreen yogScreen;
		int yogReturnCode=yogScreen.execute(globalContainer->gfx, 40);
		if (yogReturnCode==YOGScreen::CANCEL)
		{
			yog->deconnect();
			//endExecutionValue=CANCEL;
		}
		else if (yogReturnCode==-1)
		{
			yog->deconnect();
			endExecutionValue=-1;
		}
		else
			assert(false);
		printf("YOGPreScreen:: YOGScreen has ended ...\n");
		dispatchPaint(gfxCtx);
		addUpdateRect(0, 0, gfxCtx->getW(), gfxCtx->getH());
	}
	if (connectOnNextTimer)
	{
		yog->enableConnection(login->text);
		connectOnNextTimer=false;
	}
	else if (yog->externalStatusState!=oldYOGExternalStatusState)
	{
		char *s=yog->getStatusString();
		statusText->setText(s);
		delete[] s;
		oldYOGExternalStatusState=yog->externalStatusState;
	}
	
}
