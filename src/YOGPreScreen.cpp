/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
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
	addWidget(new TextButton(440, 420, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[cancel]"), CANCEL, 27));
	addWidget(new TextButton(440, 360, 180, 40, NULL, -1, -1, globalContainer->menuFont, globalContainer->texts.getString("[login]"), LOGIN, 13));

	textInput=new TextInput(20, 435, 400, 25, globalContainer->standardFont, globalContainer->userName, true);
	addWidget(textInput);
	
	statusText=new TextArea(20, 290, 600, 50, globalContainer->standardFont);
	addWidget(statusText);
	oldYOGExternalStatusState=YOG::YESTS_BAD;
	
	endExecutionValue=EXECUTING;
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
			globalContainer->yog->deconnect();
			endExecutionValue=CANCEL;
		}
		else if (par1==LOGIN)
		{
			globalContainer->yog->enableConnection(textInput->text);
		}
		else if (par1==-1)
		{
			globalContainer->yog->deconnect();
			endExecutionValue=-1;
		}
		else
			assert(false);
	}
}

void YOGPreScreen::paint(int x, int y, int w, int h)
{
	gfxCtx->drawFilledRect(x, y, w, h, 0, 0, 0);
	if (y<40)
	{
		char *text= globalContainer->texts.getString("[yog]");
		gfxCtx->drawString(20+((600-globalContainer->menuFont->getStringWidth(text))>>1), 18, globalContainer->menuFont, text);
	}
	addUpdateRect();
}

void YOGPreScreen::onTimer(Uint32 tick)
{
	globalContainer->yog->step();
	if (endExecutionValue!=EXECUTING)
	{
		if (globalContainer->yog->yogGlobalState<=YOG::YGS_NOT_CONNECTING)
			endExecute(endExecutionValue);
	}
	else if (globalContainer->yog->yogGlobalState>=YOG::YGS_CONNECTED)
	{
		printf("YOGPreScreen:: starting YOGScreen...\n");
		YOGScreen yogScreen;
		int yogReturnCode=yogScreen.execute(globalContainer->gfx, 50);
		if (yogReturnCode==YOGScreen::CANCEL)
		{
			globalContainer->yog->deconnect();
			//endExecutionValue=CANCEL;
		}
		else if (yogReturnCode==-1)
		{
			globalContainer->yog->deconnect();
			endExecutionValue=-1;
		}
		else
			assert(false);
		printf("YOGPreScreen:: YOGScreen has ended ...\n");
		dispatchPaint(gfxCtx);
	}
	if (globalContainer->yog->externalStatusState!=oldYOGExternalStatusState)
	{
		char *s=globalContainer->yog->getStatusString();
		statusText->setText(s);
		delete[] s;
		oldYOGExternalStatusState=globalContainer->yog->externalStatusState;
	}
	
}







