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

#include "MapScript.h"

#include <algorithm>
#include "boost/lexical_cast.hpp"


ScriptEditorScreen::ScriptEditorScreen(Game *game)
:OverlayScreen(globalContainer->gfx, 600, 400),
	sgslMapScript(&game->sgslScript),
	mapScript(&game->mapscript),
	game(game)
{
	addWidget(new TextButton(10, 370, 100, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[ok]"), OK));
	addWidget(new TextButton(120, 370, 100, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL));
	addWidget(new TextButton(10, 10, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[map script]"), TAB_SCRIPT));
	addWidget(new TextButton(130, 10, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[objectives]"), TAB_OBJECTIVES));
	addWidget(new TextButton(250, 10, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[briefing]"), TAB_BRIEFING));
	addWidget(new TextButton(370, 10, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[hints]"), TAB_HINTS));
	mode = new Text(20, 10, ALIGN_RIGHT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[map script]"));
	addWidget(mode);

	//These are for the script tab
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
	{
		// USL
		scriptEditor = new TextArea(10, 38, 580, 300, ALIGN_LEFT, ALIGN_TOP, "standard", false, mapScript->getMapScript().c_str());
	}
	else
	{
		// SGSL
		scriptEditor = new TextArea(10, 38, 580, 300, ALIGN_LEFT, ALIGN_TOP, "standard", false, sgslMapScript->sourceCode.c_str());
	}
	scriptWidgets.push_back(scriptEditor);
	compilationResult=new Text(10, 343, ALIGN_LEFT, ALIGN_TOP, "standard");
	scriptWidgets.push_back(compilationResult);
	scriptWidgets.push_back(new TextButton(230, 370, 130, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[compile]"), COMPILE));
	scriptWidgets.push_back(new TextButton(370, 370, 100, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[load]"), LOAD));
	cursorPosition=new Text(480, 343, ALIGN_LEFT, ALIGN_TOP, "standard", "Line:1 Col:1");
	scriptWidgets.push_back(cursorPosition);
	scriptWidgets.push_back(new TextButton(480, 370, 100, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Save]"), SAVE));

	//These are for the objectives tab
	objectivesWidgets.push_back(new TextButton(30, 40, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Primary Objectives]"), TAB_PRIMARY));
	objectivesWidgets.push_back(new TextButton(150, 40, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard", Toolkit::getStringTable()->getString("[Secondary Objectives]"), TAB_SECONDARY));

	for(int i=0; i<8; ++i)
	{
		primaryObjectives[i] = new TextInput(30, 68 + 35*i, 560, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
		objectivesWidgets.push_back(primaryObjectives[i]);

		primaryObjectiveLabels[i] = new Text(10, 68 + 35*i, ALIGN_LEFT, ALIGN_TOP, "standard", boost::lexical_cast<std::string>(i+1));
		objectivesWidgets.push_back(primaryObjectiveLabels[i]);

		secondaryObjectives[i] = new TextInput(30, 68 + 35*i, 560, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
		objectivesWidgets.push_back(secondaryObjectives[i]);

		secondaryObjectiveLabels[i] = new Text(10, 68 + 35*i, ALIGN_LEFT, ALIGN_TOP, "standard", boost::lexical_cast<std::string>(i+9));
		objectivesWidgets.push_back(secondaryObjectiveLabels[i]);
	}

	//This is for the briefing tab
	missionBriefing = new TextArea(10, 38, 580, 300, ALIGN_LEFT, ALIGN_TOP, "standard", false, game->missionBriefing.c_str());
	briefingWidgets.push_back(missionBriefing);

	//This is for the hints tab
	for(int i=0; i<8; ++i)
	{
		hints[i] = new TextInput(30, 68 + 35*i, 560, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
		hintWidgets.push_back(hints[i]);

		hintLabels[i] = new Text(10, 68 + 35*i, ALIGN_LEFT, ALIGN_TOP, "standard", boost::lexical_cast<std::string>(i+1));
		hintWidgets.push_back(hintLabels[i]);
	}

	//Add all the widgets
	for(unsigned int i=0; i<scriptWidgets.size(); ++i)
	{
		addWidget(scriptWidgets[i]);
	}
	for(unsigned int i=0; i<objectivesWidgets.size(); ++i)
	{
		objectivesWidgets[i]->visible=false;
		addWidget(objectivesWidgets[i]);
	}
	for(unsigned int i=0; i<briefingWidgets.size(); ++i)
	{
		briefingWidgets[i]->visible=false;
		addWidget(briefingWidgets[i]);
	}
	for(unsigned int i=0; i<hintWidgets.size(); ++i)
	{
		hintWidgets[i]->visible=false;
		addWidget(hintWidgets[i]);
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
	for(int i = 0; i<game->gameHints.getNumberOfHints(); ++i)
	{
		hints[game->gameHints.getScriptNumber(i)-1]->setText(game->gameHints.getGameHintText(i));
	}

	changeTabAgain=true;
}

bool ScriptEditorScreen::testCompile(void)
{
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
	{
		// USL
		mapScript->setMapScript(scriptEditor->getText());
		if(mapScript->compileCode())
		{
			compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 100, 255, 100));
			compilationResult->setText("Compilation success");
			return true;
		}
		else
		{
			MapScriptError error = mapScript->getError();
			compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
			compilationResult->setText(FormatableString("Error at %0:%1: %2").arg(error.getLine()).arg(error.getColumn()).arg(error.getMessage()).c_str());
			// USL counts from 1, TextArea counts from 0.
			scriptEditor->setCursorPos(error.getLine() - 1, error.getColumn() - 1);
			return false;
		}
	}
	else
	{
		// SGSL
		sgslMapScript->reset();
		const ErrorReport er = sgslMapScript->compileScript(game, scriptEditor->getText().c_str());
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
}

void ScriptEditorScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1 == OK)
		{
			//Load the script
			if (testCompile())
			{
				if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
				{
					// USL
					mapScript->setMapScript(scriptEditor->getText());
				}
				else
				{
					// SGSL
					sgslMapScript->sourceCode = scriptEditor->getText();
				}
				endValue=par1;
			}

			//Load the objectives
			int n=0;
			for(int i=0; i<8; ++i)
			{
				if(primaryObjectives[i]->getText() != "")
				{
					if(n >= game->objectives.getNumberOfObjectives())
					{
						game->objectives.addNewObjective(primaryObjectives[i]->getText(), false, false, false, GameObjectives::Primary, i+1);
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
						game->objectives.addNewObjective(secondaryObjectives[i]->getText(), false, false, false, GameObjectives::Secondary, i+9);
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

			//Load the briefing
			game->missionBriefing = missionBriefing->getText();

			//Load the hints
			n=0;
			for(int i=0; i<8; ++i)
			{
				if(hints[i]->getText() != "")
				{
					if(n >= game->gameHints.getNumberOfHints())
					{
						game->gameHints.addNewHint(hints[i]->getText(), false, i+1);
					}
					else
					{
						game->gameHints.setGameHintText(n, hints[i]->getText());
						game->gameHints.setScriptNumber(n, i+1);
					}
					n+=1;
				}
			}
			while(game->gameHints.getNumberOfHints() > n)
			{
				game->gameHints.removeHint(game->gameHints.getNumberOfHints()-1);
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
			if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
			{
				// USL
				loadSave(true, "scripts", "usl");
			}
			else
			{
				// SGSL
				loadSave(true, "scripts", "sgsl");
			}
		}
		else if (par1 == SAVE)
		{
			if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
			{
				// USL
				loadSave(false, "scripts", "usl");
			}
			else
			{
				// SGSL
				loadSave(false, "scripts", "sgsl");
			}
		}
		else if (par1 == TAB_SCRIPT)
		{
			for(unsigned int i=0; i<scriptWidgets.size(); ++i)
			{
				scriptWidgets[i]->visible=true;
			}
			for(unsigned int i=0; i<objectivesWidgets.size(); ++i)
			{
				objectivesWidgets[i]->visible=false;
			}
			for(unsigned int i=0; i<briefingWidgets.size(); ++i)
			{
				briefingWidgets[i]->visible=false;
			}
			for(unsigned int i=0; i<hintWidgets.size(); ++i)
			{
				hintWidgets[i]->visible=false;
			}

			mode->setText(Toolkit::getStringTable()->getString("[map script]"));
		}
		else if (par1 == TAB_OBJECTIVES)
		{
			for(unsigned int i=0; i<scriptWidgets.size(); ++i)
			{
				scriptWidgets[i]->visible=false;
			}
			for(unsigned int i=0; i<objectivesWidgets.size(); ++i)
			{
				objectivesWidgets[i]->visible=true;
			}
			for(unsigned int i=0; i<briefingWidgets.size(); ++i)
			{
				briefingWidgets[i]->visible=false;
			}
			for(unsigned int i=0; i<hintWidgets.size(); ++i)
			{
				hintWidgets[i]->visible=false;
			}

			for(int i=0; i<8; ++i)
			{
				secondaryObjectives[i]->visible = false;
				secondaryObjectiveLabels[i]->visible = false;
			}

			mode->setText(Toolkit::getStringTable()->getString("[objectives]"));
		}
		else if (par1 == TAB_BRIEFING)
		{
			for(unsigned int i=0; i<scriptWidgets.size(); ++i)
			{
				scriptWidgets[i]->visible=false;
			}
			for(unsigned int i=0; i<objectivesWidgets.size(); ++i)
			{
				objectivesWidgets[i]->visible=false;
			}
			for(unsigned int i=0; i<briefingWidgets.size(); ++i)
			{
				briefingWidgets[i]->visible=true;
			}
			for(unsigned int i=0; i<hintWidgets.size(); ++i)
			{
				hintWidgets[i]->visible=false;
			}

			mode->setText(Toolkit::getStringTable()->getString("[briefing]"));
		}
		else if (par1 == TAB_HINTS)
		{
			for(unsigned int i=0; i<scriptWidgets.size(); ++i)
			{
				scriptWidgets[i]->visible=false;
			}
			for(unsigned int i=0; i<objectivesWidgets.size(); ++i)
			{
				objectivesWidgets[i]->visible=false;
			}
			for(unsigned int i=0; i<briefingWidgets.size(); ++i)
			{
				briefingWidgets[i]->visible=false;
			}
			for(unsigned int i=0; i<hintWidgets.size(); ++i)
			{
				hintWidgets[i]->visible=true;
			}

			mode->setText(Toolkit::getStringTable()->getString("[hints]"));
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
			if(source == primaryObjectives[i] || source == secondaryObjectives[i] || hints[i])
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
				if(source != hints[i])
				{
					hints[i]->deactivate();
				}
			}
		}
	}
	else if(action == TEXT_TABBED)
	{
		TextInput* next=NULL;
		for(int i=0; i<8; ++i)
		{
			if(source == primaryObjectives[i])
			{
				next=primaryObjectives[(i+1)%8];
				break;
			}
			else if(source == secondaryObjectives[i])
			{
				next=secondaryObjectives[(i+1)%8];
				break;
			}
			else if(source == hints[i])
			{
				next=hints[(i+1)%8];
				break;
			}
		}
		if(next && changeTabAgain)
		{
			next->activate();
			for(int i=0; i<8; ++i)
			{
				if(next != primaryObjectives[i])
				{
					primaryObjectives[i]->deactivate();
				}
				if(next != secondaryObjectives[i])
				{
					secondaryObjectives[i]->deactivate();
				}
				if(next != hints[i])
				{
					hints[i]->deactivate();
				}
			}
			changeTabAgain=false;
		}
	}
	// else if(action == TEXT_MODIFIED)
	// {
	// 	// on typing compilation
	// 	if (source == scriptEditor)
	// 	{
	// 		testCompile();
	// 		unsigned line;
	// 		unsigned column;
	// 		scriptEditor->getCursorPos(line, column);
	// 		cursorPosition->setText(FormatableString("Line: %0 Col: %1").arg(line+1).arg(column+1));
	// 	}
	// }
	else if ((action == TEXT_CURSOR_MOVED) || (action == TEXT_MODIFIED))
	{
		if (source == scriptEditor)
		{
			unsigned line;
			unsigned column;
			scriptEditor->getCursorPos(line, column);
			cursorPosition->setText(FormatableString("Line: %0 Col: %1").arg(line+1).arg(column+1));
		}
	}
}

void ScriptEditorScreen::onSDLEvent(SDL_Event *event)
{
	// No unicode representation for F9 key, so putting it here.
	if ((event->type == SDL_KEYUP) && (event->key.keysym.sym == SDLK_F9))
		testCompile();
}

void ScriptEditorScreen::onTimer(Uint32 timer)
{
	changeTabAgain=true;
}


std::string filenameToName(const std::string& fullfilename)
{
	std::string filename = fullfilename;
	filename.erase(0, 8);
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
	{
		// USL
		filename.erase(filename.find(".usl"));
	}
	else
	{
		// SGSL
		filename.erase(filename.find(".sgsl"));
	}
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
				else
					testCompile();
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
