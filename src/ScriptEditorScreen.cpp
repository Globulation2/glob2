// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "ScriptEditorScreen.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUILoadSave.h"
#include "Utilities.h"

#include <FormatableString.h>
#include <Toolkit.h>
#include <StringTable.h>
using namespace GAGCore;
#include <GUIText.h>
#include <GUITextArea.h>
#include <GUIButton.h>
#include <GUITextInput.h>
using namespace GAGGUI;

#include "SDLCompat.h"

#include "MapScript.h"
#include "EditorTouchWidgets.h"
#include "MobileSafeArea.h"

#include <algorithm>
#include <string>

ScriptEditorScreen::ScriptEditorScreen(Game *game)
	: OverlayScreen(globalContainer->gfx, 600, 400), sgslMapScript(&game->sgslScript),
	  mapScript(&game->mapscript), game(game)
{
	addWidget(new TextButton(10, 370, 100, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
							 Toolkit::getStringTable()->getString("[ok]"), OK));
	addWidget(new TextButton(120, 370, 100, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
							 Toolkit::getStringTable()->getString("[Cancel]"), CANCEL));
	addWidget(new TextButton(10, 10, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
							 Toolkit::getStringTable()->getString("[map script]"), TAB_SCRIPT));
	addWidget(new TextButton(130, 10, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
							 Toolkit::getStringTable()->getString("[objectives]"), TAB_OBJECTIVES));
	addWidget(new TextButton(250, 10, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
							 Toolkit::getStringTable()->getString("[briefing]"), TAB_BRIEFING));
	addWidget(new TextButton(370, 10, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
							 Toolkit::getStringTable()->getString("[hints]"), TAB_HINTS));
	mode = new Text(20, 10, ALIGN_RIGHT, ALIGN_TOP, "standard",
					Toolkit::getStringTable()->getString("[map script]"));
	addWidget(mode);

	//These are for the script tab
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
	{
		// USL
		scriptEditor =
			new EditorTouch::TextCanvas(10, 38, 580, 300, ALIGN_LEFT, ALIGN_TOP, "standard", false,
										mapScript->getMapScript().c_str());
	}
	else
	{
		// SGSL
		scriptEditor =
			new EditorTouch::TextCanvas(10, 38, 580, 300, ALIGN_LEFT, ALIGN_TOP, "standard", false,
										sgslMapScript->sourceCode.c_str());
	}
	scriptWidgets.push_back(scriptEditor);
	compilationResult = new Text(10, 343, ALIGN_LEFT, ALIGN_TOP, "standard");
	scriptWidgets.push_back(compilationResult);
	scriptWidgets.push_back(new TextButton(230, 370, 130, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
										   Toolkit::getStringTable()->getString("[compile]"),
										   COMPILE));
	scriptWidgets.push_back(new TextButton(370, 370, 100, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
										   Toolkit::getStringTable()->getString("[load]"), LOAD));
	cursorPosition = new Text(480, 343, ALIGN_LEFT, ALIGN_TOP, "standard", "Line:1 Col:1");
	scriptWidgets.push_back(cursorPosition);
	scriptWidgets.push_back(new TextButton(480, 370, 100, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
										   Toolkit::getStringTable()->getString("[Save]"), SAVE));

	//These are for the objectives tab
	objectivesWidgets.push_back(
		new TextButton(30, 40, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
					   Toolkit::getStringTable()->getString("[Primary Objectives]"), TAB_PRIMARY));
	objectivesWidgets.push_back(new TextButton(
		150, 40, 120, 20, ALIGN_LEFT, ALIGN_TOP, "standard",
		Toolkit::getStringTable()->getString("[Secondary Objectives]"), TAB_SECONDARY));

	for (int i = 0; i < 8; ++i)
	{
		primaryObjectives[i] =
			new TextInput(30, 68 + 35 * i, 560, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
		objectivesWidgets.push_back(primaryObjectives[i]);

		primaryObjectiveLabels[i] =
			new Text(10, 68 + 35 * i, ALIGN_LEFT, ALIGN_TOP, "standard", std::to_string(i + 1));
		objectivesWidgets.push_back(primaryObjectiveLabels[i]);

		secondaryObjectives[i] =
			new TextInput(30, 68 + 35 * i, 560, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
		objectivesWidgets.push_back(secondaryObjectives[i]);

		secondaryObjectiveLabels[i] =
			new Text(10, 68 + 35 * i, ALIGN_LEFT, ALIGN_TOP, "standard", std::to_string(i + 9));
		objectivesWidgets.push_back(secondaryObjectiveLabels[i]);
	}

	//This is for the briefing tab
	missionBriefing = new EditorTouch::TextCanvas(10, 38, 580, 300, ALIGN_LEFT, ALIGN_TOP,
												  "standard", false, game->missionBriefing.c_str());
	briefingWidgets.push_back(missionBriefing);

	//This is for the hints tab
	for (int i = 0; i < 8; ++i)
	{
		hints[i] = new TextInput(30, 68 + 35 * i, 560, 25, ALIGN_LEFT, ALIGN_TOP, "standard", "");
		hintWidgets.push_back(hints[i]);

		hintLabels[i] =
			new Text(10, 68 + 35 * i, ALIGN_LEFT, ALIGN_TOP, "standard", std::to_string(i + 1));
		hintWidgets.push_back(hintLabels[i]);
	}

	//Add all the widgets
	for (unsigned int i = 0; i < scriptWidgets.size(); ++i)
	{
		addWidget(scriptWidgets[i]);
	}
	for (unsigned int i = 0; i < objectivesWidgets.size(); ++i)
	{
		objectivesWidgets[i]->visible = false;
		addWidget(objectivesWidgets[i]);
	}
	for (unsigned int i = 0; i < briefingWidgets.size(); ++i)
	{
		briefingWidgets[i]->visible = false;
		addWidget(briefingWidgets[i]);
	}
	for (unsigned int i = 0; i < hintWidgets.size(); ++i)
	{
		hintWidgets[i]->visible = false;
		addWidget(hintWidgets[i]);
	}

	touchEntry =
		new EditorTouch::TextCanvas(0, 0, 300, 200, ALIGN_LEFT, ALIGN_TOP, "standard", false, "");
	touchEntry->visible = false;
	addWidget(touchEntry);

	// important, widgets must be initialised by hand as we use custom event loop
	dispatchInit();

	for (int i = 0; i < game->objectives.getNumberOfObjectives(); ++i)
	{
		if (game->objectives.getObjectiveType(i) == GameObjectives::Primary)
		{
			primaryObjectives[game->objectives.getScriptNumber(i) - 1]->setText(
				game->objectives.getGameObjectiveText(i));
		}
		else
		{
			secondaryObjectives[game->objectives.getScriptNumber(i) - 9]->setText(
				game->objectives.getGameObjectiveText(i));
		}
	}
	for (int i = 0; i < game->gameHints.getNumberOfHints(); ++i)
	{
		hints[game->gameHints.getScriptNumber(i) - 1]->setText(game->gameHints.getGameHintText(i));
	}

	changeTabAgain = true;
}

bool ScriptEditorScreen::testCompile(void)
{
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
	{
		// USL
		mapScript->setMapScript(scriptEditor->getText());
		if (mapScript->compileCode())
		{
			compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 100, 255, 100));
			compilationResult->setText("Compilation success");
			return true;
		}
		else
		{
			MapScriptError error = mapScript->getError();
			compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
			compilationResult->setText(FormattableString("Error at %0:%1: %2")
										   .arg(error.getLine())
										   .arg(error.getColumn())
										   .arg(error.getMessage())
										   .c_str());
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
		if (er.type == ErrorReport::ET_OK)
		{
			compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 100, 255, 100));
			compilationResult->setText("Compilation success");
			return true;
		}
		else
		{
			compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
			compilationResult->setText(FormattableString("Compilation failure : %0:%1:(%2):%3")
										   .arg(er.line + 1)
										   .arg(er.col)
										   .arg(er.pos)
										   .arg(er.getErrorString())
										   .c_str());
			scriptEditor->setCursorPos(er.pos);
			return false;
		}
	}
}

void ScriptEditorScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		flushTouchEntry();
		if (par1 == HIDE_KEYBOARD)
		{
			deactivateTouchText();
			return;
		}
		if (par1 >= TAB_SCRIPT && par1 <= TAB_HINTS)
			touchTab = par1;
		if (par1 == TAB_PRIMARY || par1 == TAB_SECONDARY)
			touchSecondary = par1 == TAB_SECONDARY;
		if (par1 == OK)
		{
			//Load the script
			if (testCompile())
			{
				if ((globalContainer->settings.optionFlags &
					 GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
				{
					// USL
					mapScript->setMapScript(scriptEditor->getText());
				}
				else
				{
					// SGSL
					sgslMapScript->sourceCode = scriptEditor->getText();
				}
				endValue = par1;
			}

			//Load the objectives
			int n = 0;
			for (int i = 0; i < 8; ++i)
			{
				if (primaryObjectives[i]->getText() != "")
				{
					if (n >= game->objectives.getNumberOfObjectives())
					{
						game->objectives.addNewObjective(primaryObjectives[i]->getText(), false,
														 false, false, GameObjectives::Primary,
														 i + 1);
					}
					else
					{
						game->objectives.setGameObjectiveText(n, primaryObjectives[i]->getText());
						game->objectives.setObjectiveType(n, GameObjectives::Primary);
						game->objectives.setScriptNumber(n, i + 1);
					}
					n += 1;
				}
			}
			for (int i = 0; i < 8; ++i)
			{
				if (secondaryObjectives[i]->getText() != "")
				{
					if (n >= game->objectives.getNumberOfObjectives())
					{
						game->objectives.addNewObjective(secondaryObjectives[i]->getText(), false,
														 false, false, GameObjectives::Secondary,
														 i + 9);
					}
					else
					{
						game->objectives.setGameObjectiveText(n, secondaryObjectives[i]->getText());
						game->objectives.setObjectiveType(n, GameObjectives::Secondary);
						game->objectives.setScriptNumber(n, i + 9);
					}
					n += 1;
				}
			}
			while (game->objectives.getNumberOfObjectives() > n)
			{
				game->objectives.removeObjective(game->objectives.getNumberOfObjectives() - 1);
			}

			//Load the briefing
			game->missionBriefing = missionBriefing->getText();

			//Load the hints
			n = 0;
			for (int i = 0; i < 8; ++i)
			{
				if (hints[i]->getText() != "")
				{
					if (n >= game->gameHints.getNumberOfHints())
					{
						game->gameHints.addNewHint(hints[i]->getText(), false, i + 1);
					}
					else
					{
						game->gameHints.setGameHintText(n, hints[i]->getText());
						game->gameHints.setScriptNumber(n, i + 1);
					}
					n += 1;
				}
			}
			while (game->gameHints.getNumberOfHints() > n)
			{
				game->gameHints.removeHint(game->gameHints.getNumberOfHints() - 1);
			}
		}
		else if (par1 == CANCEL)
		{
			endValue = par1;
		}
		else if (par1 == COMPILE)
		{
			testCompile();
		}
		else if (par1 == LOAD)
		{
			if ((globalContainer->settings.optionFlags &
				 GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
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
			if ((globalContainer->settings.optionFlags &
				 GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0)
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
			for (unsigned int i = 0; i < scriptWidgets.size(); ++i)
			{
				scriptWidgets[i]->visible = true;
			}
			for (unsigned int i = 0; i < objectivesWidgets.size(); ++i)
			{
				objectivesWidgets[i]->visible = false;
			}
			for (unsigned int i = 0; i < briefingWidgets.size(); ++i)
			{
				briefingWidgets[i]->visible = false;
			}
			for (unsigned int i = 0; i < hintWidgets.size(); ++i)
			{
				hintWidgets[i]->visible = false;
			}

			mode->setText(Toolkit::getStringTable()->getString("[map script]"));
		}
		else if (par1 == TAB_OBJECTIVES)
		{
			for (unsigned int i = 0; i < scriptWidgets.size(); ++i)
			{
				scriptWidgets[i]->visible = false;
			}
			for (unsigned int i = 0; i < objectivesWidgets.size(); ++i)
			{
				objectivesWidgets[i]->visible = true;
			}
			for (unsigned int i = 0; i < briefingWidgets.size(); ++i)
			{
				briefingWidgets[i]->visible = false;
			}
			for (unsigned int i = 0; i < hintWidgets.size(); ++i)
			{
				hintWidgets[i]->visible = false;
			}

			for (int i = 0; i < 8; ++i)
			{
				secondaryObjectives[i]->visible = false;
				secondaryObjectiveLabels[i]->visible = false;
			}

			mode->setText(Toolkit::getStringTable()->getString("[objectives]"));
		}
		else if (par1 == TAB_BRIEFING)
		{
			for (unsigned int i = 0; i < scriptWidgets.size(); ++i)
			{
				scriptWidgets[i]->visible = false;
			}
			for (unsigned int i = 0; i < objectivesWidgets.size(); ++i)
			{
				objectivesWidgets[i]->visible = false;
			}
			for (unsigned int i = 0; i < briefingWidgets.size(); ++i)
			{
				briefingWidgets[i]->visible = true;
			}
			for (unsigned int i = 0; i < hintWidgets.size(); ++i)
			{
				hintWidgets[i]->visible = false;
			}

			mode->setText(Toolkit::getStringTable()->getString("[briefing]"));
		}
		else if (par1 == TAB_HINTS)
		{
			for (unsigned int i = 0; i < scriptWidgets.size(); ++i)
			{
				scriptWidgets[i]->visible = false;
			}
			for (unsigned int i = 0; i < objectivesWidgets.size(); ++i)
			{
				objectivesWidgets[i]->visible = false;
			}
			for (unsigned int i = 0; i < briefingWidgets.size(); ++i)
			{
				briefingWidgets[i]->visible = false;
			}
			for (unsigned int i = 0; i < hintWidgets.size(); ++i)
			{
				hintWidgets[i]->visible = true;
			}

			mode->setText(Toolkit::getStringTable()->getString("[hints]"));
		}
		else if (par1 == TAB_PRIMARY)
		{
			for (int i = 0; i < 8; ++i)
			{
				primaryObjectives[i]->visible = true;
				secondaryObjectives[i]->visible = false;
				primaryObjectiveLabels[i]->visible = true;
				secondaryObjectiveLabels[i]->visible = false;
			}
		}
		else if (par1 == TAB_SECONDARY)
		{
			for (int i = 0; i < 8; ++i)
			{
				primaryObjectives[i]->visible = false;
				secondaryObjectives[i]->visible = true;
				primaryObjectiveLabels[i]->visible = false;
				secondaryObjectiveLabels[i]->visible = true;
			}
		}
	}
	else if (action == TEXT_ACTIVATED)
	{
		bool found = false;
		for (int i = 0; i < 8; ++i)
		{
			if (source == primaryObjectives[i] || source == secondaryObjectives[i] ||
				source == hints[i])
				found = true;
		}
		if (found)
		{
			for (int i = 0; i < 8; ++i)
			{
				if (source != primaryObjectives[i])
				{
					primaryObjectives[i]->deactivate();
				}
				if (source != secondaryObjectives[i])
				{
					secondaryObjectives[i]->deactivate();
				}
				if (source != hints[i])
				{
					hints[i]->deactivate();
				}
			}
		}
	}
	else if (action == TEXT_TABBED)
	{
		TextInput *next = NULL;
		for (int i = 0; i < 8; ++i)
		{
			if (source == primaryObjectives[i])
			{
				next = primaryObjectives[(i + 1) % 8];
				break;
			}
			else if (source == secondaryObjectives[i])
			{
				next = secondaryObjectives[(i + 1) % 8];
				break;
			}
			else if (source == hints[i])
			{
				next = hints[(i + 1) % 8];
				break;
			}
		}
		if (next && changeTabAgain)
		{
			next->activate();
			for (int i = 0; i < 8; ++i)
			{
				if (next != primaryObjectives[i])
				{
					primaryObjectives[i]->deactivate();
				}
				if (next != secondaryObjectives[i])
				{
					secondaryObjectives[i]->deactivate();
				}
				if (next != hints[i])
				{
					hints[i]->deactivate();
				}
			}
			changeTabAgain = false;
		}
	}
	else if ((action == TEXT_CURSOR_MOVED) || (action == TEXT_MODIFIED))
	{
		if (source == scriptEditor)
		{
			unsigned line;
			unsigned column;
			scriptEditor->getCursorPos(line, column);
			cursorPosition->setText(
				FormattableString("Line: %0 Col: %1").arg(line + 1).arg(column + 1));
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
	if (fileDialog)
		fileDialog->dispatchTimer(timer);
	else
		changeTabAgain = true;
}

ScriptEditorScreen::~ScriptEditorScreen() = default;

void ScriptEditorScreen::translateAndProcessEvent(SDL_Event *event)
{
	if (!fileDialog)
	{
		OverlayScreen::translateAndProcessEvent(event);
		return;
	}
	fileDialog->translateAndProcessEvent(event);
	if (fileDialog->endValue >= 0)
		finishFileDialog();
}

void ScriptEditorScreen::drawFileDialog()
{
	if (!fileDialog)
		return;
	fileDialog->dispatchPaint();
	globalContainer->gfx->drawSurface(fileDialog->decX, fileDialog->decY, fileDialog->getSurface());
}

//! LoadSaveScreen name-extractor callback for the script load/save dialog:
//! turn a full virtual path like "scripts/My_Map.usl" (or .sgsl, depending on
//! the map-edit language option) into the display name "My Map". Directory
//! prefix and extension are only removed when actually present, so a stray
//! file in scripts/ degrades to showing its raw name instead of throwing
//! (erase(npos) used to crash the dialog).
std::string filenameToName(const std::string &fullfilename)
{
	const bool useUSL =
		(globalContainer->settings.optionFlags & GlobalContainer::OPTION_MAP_EDIT_USE_USL) != 0;
	std::string filename = Utilities::stripSuffix(Utilities::stripPrefix(fullfilename, "scripts/"),
												  useUSL ? ".usl" : ".sgsl");
	std::replace(filename.begin(), filename.end(), '_', ' ');
	return filename;
}

void ScriptEditorScreen::loadSave(bool isLoad, const char *dir, const char *ext)
{
	if (fileDialog)
		return;
	loadingScript = isLoad;
	const std::string title =
		Toolkit::getStringTable()->getString(isLoad ? "[load script]" : "[save script]");
	fileDialog = std::make_unique<LoadSaveScreen>(dir, ext, isLoad, title,
												  game->mapHeader.getMapName().c_str(),
												  filenameToName, glob2NameToFilename);
}

void ScriptEditorScreen::finishFileDialog()
{
	// The editor stays alive and suspended while its owned child handles input.
	auto loadSaveScreen = std::move(fileDialog);
	const bool isLoad = loadingScript;

	if (loadSaveScreen->endValue == 0)
	{
		if (scriptEditor->visible)
		{
			if (isLoad)
			{
				if (!scriptEditor->load(loadSaveScreen->getFileName()))
				{
					compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
					compilationResult->setText(FormattableString("Loading script from %0 failed")
												   .arg(loadSaveScreen->getName())
												   .c_str());
				}
				else
					testCompile();
			}
			else
			{
				if (!scriptEditor->save(loadSaveScreen->getFileName()))
				{
					compilationResult->setStyle(Font::Style(Font::STYLE_NORMAL, 255, 50, 50));
					compilationResult->setText(FormattableString("Saving script to %0 failed")
												   .arg(loadSaveScreen->getName())
												   .c_str());
				}
			}
		}
	}
}

OverlayScreen *ScriptEditorScreen::phoneDialog()
{
	return fileDialog ? static_cast<OverlayScreen *>(fileDialog.get()) : this;
}

void ScriptEditorScreen::flushTouchEntry()
{
	if (touchEntryTarget)
		touchEntryTarget->setText(touchEntry->getText());
}

void ScriptEditorScreen::deactivateTouchText()
{
	flushTouchEntry();
	touchEntry->deactivate();
	scriptEditor->deactivate();
	missionBriefing->deactivate();
	for (int i = 0; i < 8; ++i)
	{
		primaryObjectives[i]->deactivate();
		secondaryObjectives[i]->deactivate();
		hints[i]->deactivate();
	}
	touchPreedit.clear();
	SDL_StopTextInput();
}

void ScriptEditorScreen::cancelTouch()
{
	touchPointers.clear();
	touchInterrupted = false;
	heldTouchAction = -1;
	touchMoving = false;
	touchPreedit.clear();
}

void ScriptEditorScreen::prepareTouch()
{
	prepareTouch(mobileDialogSafe(globalContainer->gfx),
				 mobileKeyboardInset(globalContainer->gfx) > 0);
}

void ScriptEditorScreen::prepareTouch(GAGCore::ViewRect safe, bool keyboardVisible)
{
	flushTouchEntry();
	touchEntry->visible = touchTab == TAB_OBJECTIVES || touchTab == TAB_HINTS;
	if (touchEntry->visible)
	{
		auto *target = touchTab == TAB_HINTS ? hints[touchItem]
					   : touchSecondary      ? secondaryObjectives[touchItem]
											 : primaryObjectives[touchItem];
		if (target != touchEntryTarget)
		{
			touchEntryTarget = target;
			touchEntry->setText(target->getText());
		}
	}
	else
		touchEntryTarget = nullptr;
	auto *context = globalContainer->gfx;
	const double unit = context->logicalUnitsPerPoint();

	compactKeyboardWorkspace = keyboardVisible && safe.h < 240 * unit;
	if (compactKeyboardWorkspace)
	{
		const double margin = 4 * unit;
		const double width = std::max(0., std::min(safe.w - 2 * margin, 960 * unit));
		const double height = std::max(0., safe.h - 2 * margin);
		touchBounds = {safe.x + (safe.w - width) / 2, safe.y + margin, width, height};
		const double bar = std::min(44 * unit, std::max(0., height - 2 * margin));
		const double buttonWidth = std::min(136 * unit, std::max(0., width - 2 * margin));
		touchControls = {{{touchBounds.x + width - margin - buttonWidth, touchBounds.y + margin,
						   buttonWidth, bar},
						  HIDE_KEYBOARD}};
		touchContent = {touchBounds.x + margin, touchBounds.y + margin + bar + 4 * unit,
						std::max(0., width - 2 * margin),
						std::max(0., height - 2 * margin - bar - 4 * unit)};
		return;
	}
	const double width = std::min(safe.w - 16 * unit, 960 * unit);
	const double height = std::min(safe.h - 16 * unit, 720 * unit);
	touchBounds = {safe.x + (safe.w - width) / 2, safe.y + (safe.h - height) / 2, width, height};
	const double x = touchBounds.x + 8 * unit, w = touchBounds.w - 16 * unit;
	const double row = 44 * unit, gap = 4 * unit;
	touchControls.clear();
	for (int i = 0; i < 4; ++i)
		touchControls.push_back(
			{{x + i * w / 4, touchBounds.y + 8 * unit, w / 4 - gap, row}, TAB_SCRIPT + i});
	const double footer = touchBounds.y + touchBounds.h - row - 8 * unit;
	const bool narrowScript = touchTab == TAB_SCRIPT && w < 440 * unit;
	const int count = touchTab == TAB_SCRIPT && !narrowScript ? 5 : 2;
	const int actions[] = {CANCEL, OK, COMPILE, LOAD, SAVE};
	for (int i = 0; i < count; ++i)
		touchControls.push_back({{x + i * w / count, footer, w / count - gap, row}, actions[i]});
	double top = touchBounds.y + row + 16 * unit;
	if (narrowScript)
	{
		for (int i = 0; i < 3; ++i)
			touchControls.push_back({{x + i * w / 3, top, w / 3 - gap, row}, actions[i + 2]});
		top += row + gap;
	}
	touchContent = {x, top, w, std::max(24 * unit, footer - top - 28 * unit)};
	if (touchTab == TAB_OBJECTIVES || touchTab == TAB_HINTS)
	{
		// Slot strip retains stable script IDs; the selected entry gets a real
		// editing workspace instead of sixteen unrelated form rows.
		if (touchTab == TAB_OBJECTIVES)
		{
			touchControls.push_back({{x, top, w / 2 - gap, row}, TAB_PRIMARY});
			touchControls.push_back({{x + w / 2, top, w / 2 - gap, row}, TAB_SECONDARY});
			touchContent.y += row + gap;
			touchContent.h -= row + gap;
		}
		// Narrow canvases use full-sized previous/next controls. The central
		// label keeps the original script ID visible without tiny slot targets.
		if (w < 8 * (44 * unit + gap))
		{
			touchControls.push_back({{x, touchContent.y, w / 3 - gap, row}, PREVIOUS_ENTRY});
			touchControls.push_back(
				{{x + w / 3, touchContent.y, w / 3 - gap, row}, 100 + touchItem});
			touchControls.push_back(
				{{x + 2 * w / 3, touchContent.y, w / 3 - gap, row}, NEXT_ENTRY});
		}
		else
			for (int i = 0; i < 8; ++i)
				touchControls.push_back(
					{{x + i * w / 8, touchContent.y, w / 8 - gap, row}, 100 + i});
		touchContent.y += row + gap;
		touchContent.h = std::max(24 * unit, touchContent.h - row - gap);
	}
}

void ScriptEditorScreen::drawTouch()
{
	drawTouchInViewport(mobileDialogSafe(globalContainer->gfx),
						mobileKeyboardInset(globalContainer->gfx) > 0);
}

void ScriptEditorScreen::drawTouchInViewport(GAGCore::ViewRect available, bool keyboardVisible)
{
	prepareTouch(available, keyboardVisible);
	auto *context = globalContainer->gfx;
	auto *original = gfx;
	gfx = context;
	EditorTouch::panel(context, touchBounds);
	auto *strings = Toolkit::getStringTable();
	const char *tabs[] = {"[map script]", "[objectives]", "[briefing]", "[hints]"};
	for (const auto &c : touchControls)
	{
		std::string caption;
		bool selected = false;
		if (c.action >= TAB_SCRIPT && c.action <= TAB_HINTS)
		{
			caption = strings->getString(tabs[c.action - TAB_SCRIPT]);
			if (c.action == TAB_SCRIPT)
				caption = "Script";
			if (c.action == TAB_OBJECTIVES &&
				Toolkit::getFont("standard")->getStringWidth(caption) >
					c.bounds.w / context->logicalUnitsPerPoint() - 12)
				caption = strings->getString("[Goals]");
			selected = c.action == touchTab;
		}
		else if (c.action >= 100)
		{
			caption = std::to_string(c.action - 100 + 1 +
									 (touchSecondary && touchTab == TAB_OBJECTIVES ? 8 : 0));
			selected = c.action - 100 == touchItem;
		}
		else
			switch (c.action)
			{
			case PREVIOUS_ENTRY:
				caption = "Previous";
				break;
			case NEXT_ENTRY:
				caption = "Next";
				break;
			case HIDE_KEYBOARD:
				caption = "Hide keyboard";
				break;
			case OK:
				caption = strings->getString("[ok]");
				break;
			case CANCEL:
				caption = strings->getString("[Cancel]");
				break;
			case COMPILE:
				caption = "Compile";
				break;
			case LOAD:
				caption = strings->getString("[load]");
				break;
			case SAVE:
				caption = strings->getString("[Save]");
				break;
			case TAB_PRIMARY:
				caption = "Primary";
				selected = !touchSecondary;
				break;
			case TAB_SECONDARY:
				caption = "Secondary";
				selected = touchSecondary;
				break;
			}
		EditorTouch::button(context, c.bounds, caption, selected);
	}
	if (compactKeyboardWorkspace)
	{
		const double unit = context->logicalUnitsPerPoint();
		std::string section =
			touchTab == TAB_SCRIPT ? "Script"
			: touchTab == TAB_BRIEFING
				? "Briefing"
				: (touchTab == TAB_HINTS ? "Hint " : "Objective ") +
					  std::to_string(touchItem + 1 +
									 (touchSecondary && touchTab == TAB_OBJECTIVES ? 8 : 0));
		EditorTouch::label(context,
						   {touchBounds.x + 4 * unit, touchBounds.y + 4 * unit,
							std::max(0., touchBounds.w - 148 * unit), 44 * unit},
						   section);
	}
	if (touchContent.h < 24 * context->logicalUnitsPerPoint())
	{
		gfx = original;
		return;
	}
	if (touchTab == TAB_SCRIPT || touchTab == TAB_BRIEFING)
	{
		auto *canvas = static_cast<EditorTouch::TextCanvas *>(
			touchTab == TAB_SCRIPT ? scriptEditor : missionBriefing);
		auto rect = touchContent;
		if (touchTab == TAB_SCRIPT)
		{
			rect.x += 36 * context->logicalUnitsPerPoint();
			rect.w -= 36 * context->logicalUnitsPerPoint();
		}
		canvas->canvasBounds(rect);
		canvas->paintCanvas(touchTab == TAB_SCRIPT, touchPreedit);
	}
	else
	{
		auto *canvas = static_cast<EditorTouch::TextCanvas *>(touchEntry);
		canvas->canvasBounds(touchContent);
		canvas->paintCanvas(false, touchPreedit,
							touchTab == TAB_HINTS ? "Tap to write this hint"
												  : "Tap to write this objective");
	}
	if (compactKeyboardWorkspace)
	{
		gfx = original;
		return;
	}
	std::string status;
	if (touchTab == TAB_SCRIPT)
		status = compilationResult->getText().empty() ? cursorPosition->getText()
													  : compilationResult->getText();
	else if (touchTab == TAB_BRIEFING)
		status = "Mission briefing · tap to edit, swipe to scroll";
	else
		status = "Entry " + std::to_string(touchItem + 1) + " of 8 · empty entries are omitted";
	EditorTouch::label(context,
					   {touchBounds.x + 8 * context->logicalUnitsPerPoint(),
						touchBounds.y + touchBounds.h - 80 * context->logicalUnitsPerPoint(),
						touchBounds.w - 16 * context->logicalUnitsPerPoint(),
						24 * context->logicalUnitsPerPoint()},
					   status);
	gfx = original;
}

void ScriptEditorScreen::touchAction(int action)
{
	deactivateTouchText();
	if (action == HIDE_KEYBOARD)
		return;
	if (action == PREVIOUS_ENTRY || action == NEXT_ENTRY)
	{
		touchItem = (touchItem + (action == NEXT_ENTRY ? 1 : 7)) % 8;
		return;
	}
	if (action >= 100)
	{
		touchItem = action - 100;
		return;
	}
	onAction(nullptr, BUTTON_RELEASED, action, 0);
}

bool ScriptEditorScreen::eventTouch(SDL_Event event)
{
	prepareTouch();
	auto *context = globalContainer->gfx;
	auto *original = gfx;
	gfx = context;
	auto restore = [&]()
	{
		gfx = original;
		return true;
	};
	auto *canvas =
		static_cast<EditorTouch::TextCanvas *>(touchTab == TAB_SCRIPT     ? scriptEditor
											   : touchTab == TAB_BRIEFING ? missionBriefing
																		  : touchEntry);
	if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
										  event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
		cancelTouch();
	if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION ||
		event.type == SDL_FINGERUP)
	{
		const auto pointer = std::make_pair(event.tfinger.touchId, event.tfinger.fingerId);
		if (event.type == SDL_FINGERDOWN)
		{
			touchPointers.insert(pointer);
			if (touchPointers.size() > 1)
			{
				touchInterrupted = true;
				heldTouchAction = -1;
			}
		}
		else if (!touchPointers.count(pointer))
			return restore();
		if (event.type == SDL_FINGERUP)
			touchPointers.erase(pointer);
		if (touchInterrupted)
		{
			if (touchPointers.empty())
				touchInterrupted = false;
			return restore();
		}
	}
	if (event.type == SDL_TEXTEDITING)
	{
		touchPreedit = event.edit.text;
		return restore();
	}
	if (event.type == SDL_TEXTINPUT || event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
	{
		if (event.type == SDL_TEXTINPUT)
			touchPreedit.clear();
		if (event.type == SDL_KEYDOWN && !touchPreedit.empty())
			return restore();
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
		{
			deactivateTouchText();
			return restore();
		}
		dispatchEvents(&event);
		return restore();
	}
	if (event.type == SDL_MOUSEWHEEL)
	{
		canvas->scrollLines(-event.wheel.y * 3);
		return restore();
	}
	GAGCore::ViewPoint point;
	int phase = -1;
	if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION ||
		event.type == SDL_FINGERUP)
	{
		point = {event.tfinger.x * context->getW(), event.tfinger.y * context->getH()};
		phase = event.type == SDL_FINGERDOWN ? 0 : event.type == SDL_FINGERUP ? 2 : 1;
	}
	else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP)
	{
		if (event.button.which == SDL_TOUCH_MOUSEID)
			return restore();
		point = {double(event.button.x), double(event.button.y)};
		phase = event.type == SDL_MOUSEBUTTONDOWN ? 0 : 2;
	}
	else if (event.type == SDL_MOUSEMOTION)
	{
		if (event.motion.which == SDL_TOUCH_MOUSEID)
			return restore();
		point = {double(event.motion.x), double(event.motion.y)};
		phase = 1;
	}
	if (phase == 0)
	{
		touchDown = point;
		touchMoving = false;
		heldTouchAction = -1;
		for (const auto &c : touchControls)
			if (c.bounds.contains(point))
				heldTouchAction = c.action;
		if (heldTouchAction < 0 && touchContent.contains(point))
			heldTouchAction = 200;
	}
	else if (phase == 1 && heldTouchAction >= 0)
	{
		if (std::abs(point.y - touchDown.y) > 12)
		{
			touchMoving = true;
			if (heldTouchAction == 200)
			{
				canvas->scrollLines(int((touchDown.y - point.y) / 16));
				touchDown = point;
			}
		}
	}
	else if (phase == 2)
	{
		if (!touchMoving && heldTouchAction == 200 && touchContent.contains(point))
		{
			deactivateTouchText();
			canvas->placeCursor(point.x, point.y);
			SDL_Rect rect{int(touchContent.x), int(touchContent.y), int(touchContent.w),
						  int(touchContent.h)};
			SDL_SetTextInputRect(&rect);
			SDL_StartTextInput();
		}
		else if (!touchMoving)
			for (const auto &c : touchControls)
				if (c.action == heldTouchAction && c.bounds.contains(point))
				{
					touchAction(c.action);
					break;
				}
		heldTouchAction = -1;
	}
	return restore();
}
