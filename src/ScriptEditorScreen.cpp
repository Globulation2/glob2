/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include "ScriptEditorScreen.h"
#include "SGSL.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUILoadSave.h"
#include "Utilities.h"

#include <FormatableString.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <SupportFunctions.h>
#include <Stream.h>
using namespace GAGCore;
#include <GUIText.h>
#include <GUITextArea.h>
#include <GUIButton.h>
#include <GUITextInput.h>
using namespace GAGGUI;

#include <algorithm>


ScriptEditorScreen::ScriptEditorScreen(Mapscript *mapScript, Game *game)
:OverlayScreen(globalContainer->gfx, 600, 400)
{
	this->mapScript=mapScript;
	this->game=game;
	scriptEditor = new TextArea(10, 38, 580, 300, ALIGN_LEFT, ALIGN_LEFT, "standard", false, mapScript->sourceCode.c_str());
	addWidget(scriptEditor);
	campaignTextEditor = new TextArea(10, 38, 580, 300, ALIGN_LEFT, ALIGN_LEFT, "standard", false, game->campaignText.c_str());
	campaignTextEditor->visible = false;
	addWidget(campaignTextEditor);
	
	compilationResult=new Text(10, 343, ALIGN_LEFT, ALIGN_LEFT, "standard");
	addWidget(compilationResult);
	addWidget(new TextButton(10, 370, 100, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[ok]"), OK));
	addWidget(new TextButton(120, 370, 100, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL));
	compileButton = new TextButton(230, 370, 130, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[compile]"), COMPILE);
	addWidget(compileButton);
	loadButton = new TextButton(370, 370, 100, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[load]"), LOAD);
	addWidget(loadButton);
	saveButton = new TextButton(480, 370, 100, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[Save]"), SAVE);
	addWidget(saveButton);
	
	addWidget(new TextButton(10, 10, 120, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[map script]"), TAB_SCRIPT));
	addWidget(new TextButton(130, 10, 120, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[campaign text]"), TAB_CAMPAIGN_TEXT));
	addWidget(new TextButton(250, 10, 120, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[objectives]"), TAB_OBJECTIVES));
	mode = new Text(20, 10, ALIGN_RIGHT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[map script]"));
	addWidget(mode);
	
	for(int i=0; i<8; ++i)
	{
		objectives[i] = new TextInput(10, 38 + 35*i, 580, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
		if(i < game->objectives.getNumberOfObjectives())
		{
			objectives[i] = new TextInput(10, 38 + 35*i, 580, 25, ALIGN_LEFT, ALIGN_TOP, "standard", game->objectives.getGameObjectiveText(i));
		}
		else
		{
			objectives[i] = new TextInput(10, 38 + 35*i, 580, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
		
		}
		objectives[i]->visible=false;
		addWidget(objectives[i]);
	}
	
	// important, widgets must be initialised by hand as we use custom event loop
	dispatchInit();
}

bool ScriptEditorScreen::testCompile(void)
{
	mapScript->reset();
	ErrorReport er=mapScript->compileScript(game, scriptEditor->getText());

	if (er.type==ErrorReport::ET_OK)
	{
		compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 100, 255, 100));
		compilationResult->setText("Compilation success");
		return true;
	}
	else
	{
		compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
		compilationResult->setText(FormatableString("Compilation failure : %0:%1:(%2):%3").arg(er.line+1).arg(er.col).arg(er.pos).arg(er.getErrorString()).c_str());
		scriptEditor->setCursorPos(er.pos);
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
				mapScript->sourceCode = scriptEditor->getText();
				endValue=par1;
			}
			game->campaignText = campaignTextEditor->getText();
			
			int n=0;
			for(int i=0; i<8; ++i)
			{
				if(objectives[i]->getText() != "")
				{
					if(n >= game->objectives.getNumberOfObjectives())
					{
						game->objectives.addNewObjective(objectives[i]->getText(), false, false);
					}
					else
					{
						game->objectives.setGameObjectiveText(i, objectives[i]->getText());
					}
					n+=1;
				}
			}
			while(game->objectives.getNumberOfObjectives() > n)
			{
				game->objectives.removeObjective(game->objectives.getNumberOfObjectives()-1);
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
			if (scriptEditor->visible)
				loadSave(true, "scripts", "sgsl");
			else if (campaignTextEditor->visible)
				loadSave(true, "campaigns", "txt");
		}
		else if (par1 == SAVE)
		{
			if (scriptEditor->visible)
				loadSave(false, "scripts", "sgsl");
			else if (campaignTextEditor->visible)
				loadSave(false, "campaigns", "txt");
		}
		else if (par1 == TAB_SCRIPT)
		{
			scriptEditor->visible = true;
			compileButton->visible = true;
			loadButton->visible = true;
			saveButton->visible = true;
			
			campaignTextEditor->visible = false;

			for(int i=0; i<8; ++i)
			{
				objectives[i]->visible = false;
			}
			
			mode->setText(Toolkit::getStringTable()->getString("[map script]"));
		}
		else if (par1 == TAB_CAMPAIGN_TEXT)
		{
			scriptEditor->visible = false;
			compileButton->visible = false;
			loadButton->visible = false;
			saveButton->visible = false;
			
			campaignTextEditor->visible = true;

			for(int i=0; i<8; ++i)
			{
				objectives[i]->visible = false;
			}
			
			mode->setText(Toolkit::getStringTable()->getString("[campaign text]"));
		}
		else if (par1 == TAB_OBJECTIVES)
		{
			scriptEditor->visible = false;
			compileButton->visible = false;
			loadButton->visible = false;
			saveButton->visible = false;
			
			campaignTextEditor->visible = false;

			for(int i=0; i<8; ++i)
			{
				objectives[i]->visible = true;
			}
			
			mode->setText(Toolkit::getStringTable()->getString("[objectives]"));
		}
	}
	else if(action == TEXT_ACTIVATED)
	{
		bool found = false;
		for(int i=0; i<8; ++i)
		{
			if(source == objectives[i])
				found = true;
		}
		if(found)
		{
			for(int i=0; i<8; ++i)
			{
				if(source != objectives[i])
					objectives[i]->deactivate();
			}
		}
	}
}

void ScriptEditorScreen::onSDLEvent(SDL_Event *event)
{

}

std::string filenameToName(const std::string& fullfilename)
{
	std::string filename = fullfilename;
	filename.erase(0, 8);
	filename.erase(filename.find(".sgsl"));
	std::replace(filename.begin(), filename.end(), '_', ' ');
	return filename;
}

void ScriptEditorScreen::loadSave(bool isLoad, const char *dir, const char *ext)
{
	// create dialog box
	LoadSaveScreen *loadSaveScreen=new LoadSaveScreen(dir, ext, isLoad, true, game->mapHeader.getMapName().c_str(), filenameToName, glob2NameToFilename);
	loadSaveScreen->dispatchPaint();

	// save screen
	globalContainer->gfx->setClipRect();
	
	DrawableSurface *background = new DrawableSurface(globalContainer->gfx->getW(), globalContainer->gfx->getH());
	background->drawSurface(0, 0, globalContainer->gfx);

	SDL_Event event;
	while(loadSaveScreen->endValue<0)
	{
		int time = SDL_GetTicks();
		while (SDL_PollEvent(&event))
		{
			loadSaveScreen->translateAndProcessEvent(&event);
		}
		loadSaveScreen->dispatchPaint();
		
		globalContainer->gfx->drawSurface(0, 0, background);
		globalContainer->gfx->drawSurface(loadSaveScreen->decX, loadSaveScreen->decY, loadSaveScreen->getSurface());
		globalContainer->gfx->nextFrame();
		int ntime = SDL_GetTicks();
		SDL_Delay(std::max(0, 40 - ntime + time));
	}

	if (loadSaveScreen->endValue==0)
	{
		if (scriptEditor->visible)
		{
			if (isLoad)
			{
				if (!scriptEditor->load(loadSaveScreen->getFileName()))
				{
					compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
					compilationResult->setText(FormatableString("Loading script from %0 failed").arg(loadSaveScreen->getName()).c_str());
				}
			}
			else
			{
				if (!scriptEditor->save(loadSaveScreen->getFileName()))
				{
					compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
					compilationResult->setText(FormatableString("Saving script to %0 failed").arg(loadSaveScreen->getName()).c_str());
				}
			}
		}
		else if (campaignTextEditor->visible)
		{
			if (isLoad)
			{
				if (!campaignTextEditor->load(loadSaveScreen->getFileName()))
				{
					compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
					compilationResult->setText(FormatableString("Loading campaign text from %0 failed").arg(loadSaveScreen->getName()).c_str());
				}
			}
			else
			{
				if (!campaignTextEditor->save(loadSaveScreen->getFileName()))
				{
					compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
					compilationResult->setText(FormatableString("Saving campaign text to %0 failed").arg(loadSaveScreen->getName()).c_str());
				}
			}
		}
		else
			assert(false);
	}

	// clean up
	delete loadSaveScreen;
	
	// destroy temporary surface
	delete background;
}
