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
#include "boost/lexical_cast.hpp"


ScriptEditorScreen::ScriptEditorScreen(Mapscript *mapScript, Game *game)
:OverlayScreen(globalContainer->gfx, 600, 400)
{
	this->mapScript=mapScript;
	this->game=game;
	scriptEditor = new TextArea(10, 38, 580, 300, ALIGN_LEFT, ALIGN_LEFT, "standard", false, mapScript->sourceCode.c_str());
	addWidget(scriptEditor);
	
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
	addWidget(new TextButton(130, 10, 120, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[objectives]"), TAB_OBJECTIVES));

	primary = new TextButton(30, 40, 120, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[Primary Objectives]"), TAB_PRIMARY);
	secondary = new TextButton(150, 40, 120, 20, ALIGN_LEFT, ALIGN_LEFT, "standard", Toolkit::getStringTable()->getString("[Secondary Objectives]"), TAB_SECONDARY);
	addWidget(primary);
	addWidget(secondary);
	secondary->visible=false;
	primary->visible=false;
	
	mode = new Text(20, 10, ALIGN_RIGHT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[map script]"));
	addWidget(mode);
	
	for(int i=0; i<8; ++i)
	{
		primaryObjectives[i] = new TextInput(30, 68 + 35*i, 580, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
		primaryObjectives[i]->visible=false;
		addWidget(primaryObjectives[i]);
		
		primaryObjectiveLabels[i] = new Text(10, 68 + 35*i, ALIGN_LEFT, ALIGN_TOP, "standard", boost::lexical_cast<std::string>(i+1));
		primaryObjectiveLabels[i]->visible=false;
		addWidget(primaryObjectiveLabels[i]);
		
		
		secondaryObjectives[i] = new TextInput(30, 68 + 35*i, 580, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
		secondaryObjectives[i]->visible=false;
		addWidget(secondaryObjectives[i]);
		
		secondaryObjectiveLabels[i] = new Text(10, 68 + 35*i, ALIGN_LEFT, ALIGN_TOP, "standard", boost::lexical_cast<std::string>(i+9));
		secondaryObjectiveLabels[i]->visible=false;
		addWidget(secondaryObjectiveLabels[i]);
	}
	
	// important, widgets must be initialised by hand as we use custom event loop
	dispatchInit();

	for(int i = 0; i<game->objectives.getNumberOfObjectives(); ++i)
	{
		if(game->objectives.getObjectiveType(i) == GameObjectives::Primary)
		{
			primaryObjectives[game->objectives.getScriptNumber(i)-1]->setText(game->objectives.getGameObjectiveText(i));
		}
		else
		{
			secondaryObjectives[game->objectives.getScriptNumber(i)-9]->setText(game->objectives.getGameObjectiveText(i));
		}
	}
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
			
			int n=0;
			for(int i=0; i<8; ++i)
			{
				if(primaryObjectives[i]->getText() != "")
				{
					if(n >= game->objectives.getNumberOfObjectives())
					{
						game->objectives.addNewObjective(primaryObjectives[i]->getText(), false, false, GameObjectives::Primary, i+1);
					}
					else
					{
						game->objectives.setGameObjectiveText(n, primaryObjectives[i]->getText());
						game->objectives.setObjectiveType(n, GameObjectives::Primary);
						game->objectives.setScriptNumber(n, i+1);
					}
					n+=1;
				}
			}
			for(int i=0; i<8; ++i)
			{
				if(secondaryObjectives[i]->getText() != "")
				{
					if(n >= game->objectives.getNumberOfObjectives())
					{
						game->objectives.addNewObjective(secondaryObjectives[i]->getText(), false, false, GameObjectives::Secondary, i+9);
					}
					else
					{
						game->objectives.setGameObjectiveText(n, secondaryObjectives[i]->getText());
						game->objectives.setObjectiveType(n, GameObjectives::Secondary);
						game->objectives.setScriptNumber(n, i+9);
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
			loadSave(true, "scripts", "sgsl");
		}
		else if (par1 == SAVE)
		{
			loadSave(false, "scripts", "sgsl");
		}
		else if (par1 == TAB_SCRIPT)
		{
			scriptEditor->visible = true;
			compileButton->visible = true;
			loadButton->visible = true;
			saveButton->visible = true;

			primary->visible = false;
			secondary->visible = false;
			for(int i=0; i<8; ++i)
			{
				primaryObjectives[i]->visible = false;
				secondaryObjectives[i]->visible = false;
				primaryObjectiveLabels[i]->visible = false;
				secondaryObjectiveLabels[i]->visible = false;
			}
			
			mode->setText(Toolkit::getStringTable()->getString("[map script]"));
		}
		else if (par1 == TAB_OBJECTIVES)
		{
			scriptEditor->visible = false;
			compileButton->visible = false;
			loadButton->visible = false;
			saveButton->visible = false;

			primary->visible = true;
			secondary->visible = true;
			for(int i=0; i<8; ++i)
			{
				primaryObjectives[i]->visible = true;
				secondaryObjectives[i]->visible = false;
				primaryObjectiveLabels[i]->visible = true;
				secondaryObjectiveLabels[i]->visible = false;
			}
			
			mode->setText(Toolkit::getStringTable()->getString("[objectives]"));
		}
		else if (par1 == TAB_PRIMARY)
		{
			for(int i=0; i<8; ++i)
			{
				primaryObjectives[i]->visible = true;
				secondaryObjectives[i]->visible = false;
				primaryObjectiveLabels[i]->visible = true;
				secondaryObjectiveLabels[i]->visible = false;
			}
		}
		else if (par1 == TAB_SECONDARY)
		{
			for(int i=0; i<8; ++i)
			{
				primaryObjectives[i]->visible = false;
				secondaryObjectives[i]->visible = true;
				primaryObjectiveLabels[i]->visible = false;
				secondaryObjectiveLabels[i]->visible = true;
			}
		}
	}
	else if(action == TEXT_ACTIVATED)
	{
		bool found = false;
		for(int i=0; i<8; ++i)
		{
			if(source == primaryObjectives[i] || source == secondaryObjectives[i])
				found = true;
		}
		if(found)
		{
			for(int i=0; i<8; ++i)
			{
				if(source != primaryObjectives[i])
				{
					primaryObjectives[i]->deactivate();
				}
				if(source != secondaryObjectives[i])
				{
					secondaryObjectives[i]->deactivate();
				}
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
	}

	// clean up
	delete loadSaveScreen;
	
	// destroy temporary surface
	delete background;
}
