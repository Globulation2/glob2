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

#include "ScriptEditorScreen.h"
#include "GlobalContainer.h"

//! Main menu screen
ScriptEditorScreen::ScriptEditorScreen(Mapscript *mapScript, Game *game)
:OverlayScreen(globalContainer->gfx, 600, 400)
{
	this->mapScript=mapScript;
	this->game=game;
	editor = new TextArea(10, 10, 580, 320, globalContainer->standardFont, false, mapScript->getSourceCode());
	addWidget(editor);
	compilationResult=new Text(10, 335, globalContainer->standardFont);
	addWidget(compilationResult);
	addWidget(new TextButton(10, 360, 80, 30, NULL, -1, -1, globalContainer->standardFont, globalContainer->texts.getString("[ok]"), OK));
	addWidget(new TextButton(100, 360, 80, 30, NULL, -1, -1, globalContainer->standardFont, globalContainer->texts.getString("[Cancel]"), CANCEL));
	addWidget(new TextButton(190, 360, 400, 30, NULL, -1, -1, globalContainer->standardFont, globalContainer->texts.getString("[compile]"), COMPILE));
}

bool ScriptEditorScreen::testCompile(void)
{
	/*const char *backup=mapScript->getSourceCode();
	char *temp=new char[strlen(backup+1)];
	strcpy(temp, backup);

	mapScript->setSourceCode(editor->getText());
	ErrorReport er=mapScript->compileScript(game);

	mapScript->setSourceCode(temp);
	delete[] temp;*/
	ErrorReport er=mapScript->compileScript(game, editor->getText());

	if (er.type==ErrorReport::ET_OK)
	{
		compilationResult->setColor(100, 255, 100);
		compilationResult->setText("Compilation success");
		return true;
	}
	else
	{
		compilationResult->setColor(255, 50, 50);
		compilationResult->setText("Compilation failure : %d:%d:%s", er.line+1, er.col, er.getErrorString());
		editor->setCursorPos(er.pos);
		return false;
	}
}

void ScriptEditorScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1 == COMPILE)
		{
			testCompile();
		}
		else
		{
			if (par1 == OK)
			{
				if (testCompile())
				{
					mapScript->setSourceCode(editor->getText());
					endValue=par1;
				}
			}
			else
				endValue=par1;
		}
	}
}

void ScriptEditorScreen::onSDLEvent(SDL_Event *event)
{

}
