/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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
#include "SGSL.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUILoadSave.h"
#include "Utilities.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <SupportFunctions.h>
using namespace GAGCore;
#include <GUIText.h>
#include <GUITextArea.h>
#include <GUIButton.h>
using namespace GAGGUI;

#include <algorithm>

//! Main menu screen
ScriptEditorScreen::ScriptEditorScreen(Mapscript *mapScript, Game *game)
:OverlayScreen(globalContainer->gfx, 600, 400)
{
	this->mapScript=mapScript;
	this->game=game;
	editor = new TextArea(10, 10, 580, 320, ALIGN_LEFT, ALIGN_LEFT, "standard", false, mapScript->getSourceCode());
	addWidget(editor);
	compilationResult=new Text(10, 335, ALIGN_LEFT, ALIGN_LEFT, "standard");
	addWidget(compilationResult);
	addWidget(new TextButton(10, 360, 100, 30, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[ok]"), OK));
	addWidget(new TextButton(120, 360, 100, 30, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL));
	addWidget(new TextButton(230, 360, 130, 30, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[compile]"), COMPILE));
	addWidget(new TextButton(370, 360, 100, 30, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[load]"), LOAD));
	addWidget(new TextButton(480, 360, 100, 30, ALIGN_LEFT, ALIGN_LEFT, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[Save]"), SAVE));
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
	mapScript->reset();
	ErrorReport er=mapScript->compileScript(game, editor->getText());

	if (er.type==ErrorReport::ET_OK)
	{
		compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 100, 255, 100));
		compilationResult->setText("Compilation success");
		return true;
	}
	else
	{
		compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
		compilationResult->setText(GAGCore::nsprintf("Compilation failure : %d:%d:(%d):%s", er.line+1, er.col, er.pos, er.getErrorString()).c_str());
		editor->setCursorPos(er.pos);
		return false;
	}
}

void ScriptEditorScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1 == OK)
		{
			if (testCompile())
			{
				mapScript->setSourceCode(editor->getText());
				endValue=par1;
			}
		}
		else if (par1 == CANCEL)
		{
			endValue=par1;
		}
		else if (par1 == COMPILE)
		{
			testCompile();
		}
		else if (par1 == LOAD)
		{
			loadSave(true);
		}
		else if (par1 == SAVE)
		{
			loadSave(false);
		}
	}
}

void ScriptEditorScreen::onSDLEvent(SDL_Event *event)
{

}

const char* filenameToName(const char *fullfilename)
{
	const char* filename = strrchr(fullfilename, DIR_SEPARATOR) + 1;
	const char* filenameend = strrchr(filename, '.');
	size_t len = filenameend - filename;
	char* name = new char[len + 1];
	*std::replace_copy(filename, filenameend, name, '_', ' ') = '\0';
	return name;
}

void ScriptEditorScreen::loadSave(bool isLoad)
{
	// create dialog box
	LoadSaveScreen *loadSaveScreen=new LoadSaveScreen("scripts", "sgsl", isLoad, game->session.getMapName(), filenameToName, glob2NameToFilename);
	loadSaveScreen->dispatchPaint(loadSaveScreen->getSurface());

	// save screen
	globalContainer->gfx->setClipRect();

	SDL_Event event;
	while(loadSaveScreen->endValue<0)
	{
		while (SDL_PollEvent(&event))
		{
			loadSaveScreen->translateAndProcessEvent(&event);
		}
		globalContainer->gfx->drawSurface(loadSaveScreen->decX, loadSaveScreen->decY, loadSaveScreen->getSurface());
		globalContainer->gfx->updateRect(loadSaveScreen->decX, loadSaveScreen->decY, loadSaveScreen->getW(), loadSaveScreen->getH());
	}

	if (loadSaveScreen->endValue==0)
	{
		if (isLoad)
		{
			SDL_RWops *stream=globalContainer->fileManager->open(loadSaveScreen->getFileName(),"rb");
			if (!stream)
			{
				compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
				compilationResult->setText(GAGCore::nsprintf("Loading script from %s failed", loadSaveScreen->getName()).c_str());
				return;
			}
			SDL_RWseek(stream, 0, SEEK_END);
			unsigned len=SDL_RWtell(stream);
			SDL_RWseek(stream, 0, SEEK_SET);
			char* sourceCode=new char[len+1];
			SDL_RWread(stream, sourceCode, len, 1);
			sourceCode[len] = '\0';
			editor->setText(sourceCode);
			delete[] sourceCode;
			SDL_RWclose(stream);
		}
		else
		{
			SDL_RWops *stream=globalContainer->fileManager->open(loadSaveScreen->getFileName(),"wb");
			if (!stream)
			{
				compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
				compilationResult->setText(GAGCore::nsprintf("Saving script to %s failed", loadSaveScreen->getName()).c_str());
				return;
			}
			const char* sourceCode=editor->getText();
			SDL_RWwrite(stream, sourceCode, strlen(sourceCode), 1);
			SDL_RWclose(stream);
		}
	}

	// clean up
	delete loadSaveScreen;

	//draw();
}
