// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "ChooseMapScreen.h"
#include "FileImport.h"
#include <ApplicationHost.h>
#include "GUIGlob2FileList.h"
#include "GUIMapPreview.h"
#include "GlobalContainer.h"
#include <FormatableString.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <memory>

#include "Game.h"
#include "gui/MobileSafeArea.h"
#include <InterfacePresentation.h>

ChooseMapScreen::ChooseMapScreen(const char *directory, const char *extension, bool recurse,
								 const char *alternateDirectory, const char *alternateExtension,
								 const bool alternateRecurse)
{
	ok = new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
						Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	addWidget(ok);

	cancel = new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
							Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	addWidget(cancel);

	fileList = new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
								 "standard", directory, extension, recurse);
	addWidget(fileList);

	mapPreview =
		new MapPreview(640 - 20 - 26 - 128, 70, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	addWidget(mapPreview);

	currentDirectoryMode = DisplayRegular;

	deleteMap = NULL;
	if (strcmp(directory, "maps") == 0)
	{
		type1 = MAP;
		type2 = GAME;

		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu",
						 Toolkit::getStringTable()->getString("[choose map]"));
	}
	else if (strcmp(directory, "games") == 0)
	{
		type1 = GAME;
		type2 = REPLAY;

		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu",
						 Toolkit::getStringTable()->getString("[choose game]"));
		deleteMap =
			new TextButton(250, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
						   Toolkit::getStringTable()->getString("[delete]"), DELETEGAME);
		addWidget(deleteMap);
		if (GAGCore::ApplicationHost::canExportFiles())
		{
			exportButton =
				new TextButton(250, 300, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
							   "menu", Toolkit::getStringTable()->getString("[export file]"), 5);
			addWidget(exportButton);
		}
	}
	else
	{
		type1 = GAME;
		type2 = NONE;

		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu",
						 Toolkit::getStringTable()->getString("[choose game]"));
	}
	addWidget(title);
	mapName = new Text(440, 60 + 128 + 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard",
					   "", 180);
	addWidget(mapName);
	mapInfo = new Text(440, 60 + 128 + 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard",
					   "", 180);
	addWidget(mapInfo);
	mapVersion = new Text(440, 60 + 128 + 75, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
						  "standard", "", 180);
	addWidget(mapVersion);
	mapSize = new Text(440, 60 + 128 + 100, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
					   "standard", "", 180);
	addWidget(mapSize);
	mapDate = new Text(440, 60 + 128 + 125, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
					   "standard", "", 180);
	addWidget(mapDate);

	if (alternateDirectory)
	{
		assert(type2 != NONE);

		switchType = new TextButton(250, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
									"menu", loadableTypeName(type2).c_str(), SWITCHTYPE, 27);
		addWidget(switchType);

		alternateFileList =
			new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
							  "standard", alternateDirectory, alternateExtension, alternateRecurse);
		addWidget(alternateFileList);
		alternateFileList->visible = false;
	}

	if (GAGCore::ApplicationHost::canImportFiles())
	{
		importButton =
			new TextButton(20, 470, 85, 30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED,
						   "standard", Toolkit::getStringTable()->getString("[import file]"), 6);
		addWidget(importButton);
		if (!exportButton)
		{
			exportButton = new TextButton(115, 470, 85, 30, ALIGN_SCREEN_CENTERED,
										  ALIGN_SCREEN_CENTERED, "standard",
										  Toolkit::getStringTable()->getString("[export file]"), 5);
			addWidget(exportButton);
		}
	}
	validMapSelected = false;
	selectedType = NONE;
}

ChooseMapScreen::~ChooseMapScreen() {}

bool ChooseMapScreen::importBusy() const
{
	return fileSelection || (fileImport && (fileImport->state() == FileImport::State::Validating ||
											fileImport->state() == FileImport::State::Persisting));
}

void ChooseMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	const bool button = action == BUTTON_RELEASED || action == BUTTON_SHORTCUT;
	if (importBusy())
	{
		if (button && source == cancel &&
			(!fileImport || fileImport->state() != FileImport::State::Persisting))
		{
			fileSelection.reset();
			fileImport.reset();
			GAGCore::ApplicationHost::importChanged("cancelled");
			showPhoneStatus(title, Toolkit::getStringTable()->getString("[import cancelled]"));
		}
		return;
	}
	if (button && importButton && source == importButton)
	{
		if (fileImport && fileImport->canRetry())
		{
			fileImport->retryPersistence();
			updateImportStatus();
			return;
		}
		fileImport.reset();
		importExtension = activeType() == MAP ? "map" : activeType() == REPLAY ? "replay" : "game";
		fileSelection = GAGCore::ApplicationHost::selectFile(importExtension);
		GAGCore::ApplicationHost::importChanged("selecting");
		showPhoneStatus(title, Toolkit::getStringTable()->getString("[select import file]"));
		return;
	}
	if (button && source == exportButton && fileImport && fileImport->canRetry())
	{
		if (!fileImport->exportFile())
			showPhoneStatus(title, Toolkit::getStringTable()->getString("[export failed]"));
		return;
	}
	if (action == LIST_ELEMENT_SELECTED)
	{
		Glob2FileList *active = activeFileList();
		// Invalidate the old selection before attempting any fallible file reads.
		validMapSelected = false;
		selectedType = NONE;
		mapDate->setText("");
		mapVersion->setText("");
		mapInfo->setText("");
		mapSize->setText("");
		mapName->setText("");
		mapPreview->setMapThumbnail(MapThumbnail());
		showPhoneStatus(title, Toolkit::getStringTable()->getString(
								   type1 == MAP ? "[choose map]" : "[choose game]"));
		if (active->selection())
		{
			std::string mapFileName = active->listToFile(active->getText(par1).c_str());

			try
			{
				mapPreview->setMapThumbnail(mapFileName.c_str());

				auto stream = std::unique_ptr<InputStream>(new BinaryInputStream(Toolkit::getFileManager()->openInflatingInputStreamBackend(mapFileName)));
				if (stream->isEndOfStream())
				{
					std::cerr << "ChooseMapScreen::onAction() : error, can't open file "
							  << mapFileName << std::endl;
				}
				else
				{
					if (verbose)
						std::cout << "ChooseMapScreen::onAction : loading map " << mapFileName
								  << std::endl;
					validMapSelected = mapHeader.load(stream.get());

					if (!validMapSelected)
						selectedType = NONE;

					mapHeader.setMapName(glob2FilenameToName(mapFileName));
					if (validMapSelected)
					{
						updateMapInformation();

						time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
						mapDate->setText(ctime(&mtime));

						selectedType = activeType();
					}
					else
						std::cerr << "ChooseMapScreen::onAction : invalid map header for map "
								  << mapFileName << std::endl;
				}
			}
			catch (std::exception &e)
			{
				std::cerr << "ChooseMapScreen: " << e.what() << std::endl;
				validMapSelected = false;
				selectedType = NONE;
			}
			if (!validMapSelected)
			{
				mapPreview->setMapThumbnail(MapThumbnail());
				showPhoneStatus(title, Toolkit::getStringTable()->getString("[Damaged Map]"));
			}
		}
	}
	else if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (exportButton && source == exportButton)
		{
			auto *active = activeFileList();
			if (active->selection() &&
				!GAGCore::ApplicationHost::exportLocalFile(active->listToFile(active->get())))
				showPhoneStatus(title, Toolkit::getStringTable()->getString("[export failed]"));
		}
		else if (source == ok)
		{
			// we accept only if a valid map is selected
			if (validMapSelected)
				endExecute(OK);
		}
		else if (source == cancel)
		{
			endExecute(par1);
		}
		else if (source == deleteMap)
		{
			// if a valid file is selected, delete it
			Glob2FileList *active = activeFileList();
			if (auto sel = active->selection())
			{
				size_t i = *sel;
				std::string mapFileName = active->listToFile(active->get().c_str());

				Toolkit::getFileManager()->remove(mapFileName);
				active->generateList();

				active->setSelection(List::selectionAfterRemoval(i, active->getCount()));
				active->selectionChanged();
			}
		}
		else if (source == switchType)
		{
			setDirectoryMode(currentDirectoryMode == DisplayRegular ? DisplayAlternate
																	: DisplayRegular);
		}
	}
}

void ChooseMapScreen::updateImportStatus()
{
	const auto state = fileImport->state();
	GAGCore::ApplicationHost::importChanged(state == FileImport::State::Validating   ? "validating"
											: state == FileImport::State::Persisting ? "persisting"
											: state == FileImport::State::Succeeded  ? "succeeded"
											: fileImport->canRetry()                 ? "failed"
																					 : "invalid");
	const char *message = state == FileImport::State::Validating   ? "[validating import]"
						  : state == FileImport::State::Persisting ? "[saving to storage]"
						  : state == FileImport::State::Succeeded  ? "[import succeeded]"
						  : fileImport->canRetry()                 ? "[import persistence failed]"
																   : "[import failed]";
	showPhoneStatus(title, Toolkit::getStringTable()->getString(message));
}

void ChooseMapScreen::onTimer(Uint32)
{
	if (fileSelection)
	{
		const auto state = fileSelection->state();
		if (state == GAGCore::ApplicationHost::FileSelectionState::Pending)
			return;
		if (state == GAGCore::ApplicationHost::FileSelectionState::Selected)
		{
			try
			{
				fileImport =
					std::make_unique<FileImport>(fileSelection->takeFile(), importExtension);
			}
			catch (const std::exception &)
			{
				showPhoneStatus(title, Toolkit::getStringTable()->getString("[import failed]"));
			}
		}
		else
		{
			GAGCore::ApplicationHost::importChanged(
				state == GAGCore::ApplicationHost::FileSelectionState::Cancelled ? "cancelled"
																				 : "invalid");
			showPhoneStatus(title,
							Toolkit::getStringTable()->getString(
								state == GAGCore::ApplicationHost::FileSelectionState::Cancelled
									? "[import cancelled]"
									: "[import failed]"));
		}
		fileSelection.reset();
	}
	if (!fileImport)
		return;
	fileImport->advance();
	updateImportStatus();
	if (fileImport->state() == FileImport::State::Succeeded)
	{
		auto *active = activeFileList();
		active->generateList();
		const auto name = glob2FilenameToName(fileImport->path());
		for (unsigned i = 0; i < active->getCount(); ++i)
			if (active->getText(i) == name)
			{
				active->setSelection(i);
				active->selectionChanged();
				break;
			}
		fileImport.reset();
	}
}

void ChooseMapScreen::updateMapInformation()
{
	// update map name & info
	mapName->setText(mapHeader.getMapName());
	std::string textTemp;
	textTemp = FormattableString("%0%1")
				   .arg(mapHeader.getNumberOfTeams())
				   .arg(Toolkit::getStringTable()->getString("[teams]"));
	mapInfo->setText(textTemp);
	textTemp = FormattableString("%0 %1.%2")
				   .arg(Toolkit::getStringTable()->getString("[Version]"))
				   .arg(mapHeader.getVersionMajor())
				   .arg(mapHeader.getVersionMinor());
	mapVersion->setText(textTemp);
	textTemp = FormattableString("%0 x %1")
				   .arg(mapPreview->getLastWidth())
				   .arg(mapPreview->getLastHeight());
	mapSize->setText(textTemp);

	// call subclass handler
	validMapSelectedhandler();
}

MapHeader &ChooseMapScreen::getMapHeader()
{
	return mapHeader;
}

GameHeader &ChooseMapScreen::getGameHeader()
{
	return gameHeader;
}

ChooseMapScreen::LoadableType ChooseMapScreen::getSelectedType()
{
	return selectedType;
}

Glob2FileList *ChooseMapScreen::activeFileList() const
{
	return (currentDirectoryMode == DisplayRegular) ? fileList : alternateFileList;
}

ChooseMapScreen::LoadableType ChooseMapScreen::activeType() const
{
	return (currentDirectoryMode == DisplayRegular) ? type1 : type2;
}

std::string ChooseMapScreen::loadableTypeName(LoadableType type)
{
	switch (type)
	{
	case GAME:
		return Toolkit::getStringTable()->getString("[the games]");
	case MAP:
		return Toolkit::getStringTable()->getString("[the maps]");
	case REPLAY:
		return Toolkit::getStringTable()->getString("[the replays]");
	case NONE:
		break;
	}
	assert(false);
	return {};
}

void ChooseMapScreen::setDirectoryMode(DirectoryMode newMode)
{
	currentDirectoryMode = newMode;
	const bool regular = (newMode == DisplayRegular);
	fileList->visible = regular;
	alternateFileList->visible = !regular;
	// After switching, the button label points back to the list we just left.
	switchType->setText(loadableTypeName(regular ? type2 : type1));
	activeFileList()->selectionChanged();
}

bool ChooseMapScreen::usesResponsiveViewport() const
{
	return phonePresentationRequested();
}
void ChooseMapScreen::handleExecutionEvent(SDL_Event event)
{
	if (usesResponsiveViewport())
	{
		if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
			event.button.which == SDL_TOUCH_MOUSEID)
			return;
		if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERUP ||
			event.type == SDL_FINGERMOTION)
		{
			const auto finger = event.tfinger;
			const auto kind = event.type;
			event = {};
			if (kind == SDL_FINGERMOTION)
			{
				event.type = SDL_MOUSEMOTION;
				event.motion.state = SDL_BUTTON_LMASK;
				event.motion.x = int(finger.x * getW());
				event.motion.y = int(finger.y * getH());
			}
			else
			{
				event.type = kind == SDL_FINGERDOWN ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
				event.button.button = SDL_BUTTON_LEFT;
				event.button.x = int(finger.x * getW());
				event.button.y = int(finger.y * getH());
			}
		}
	}
	Screen::handleExecutionEvent(event);
}
void ChooseMapScreen::paint()
{
    fileList->setMinimumRowHeight(usesResponsiveViewport()?44:0);
    if(alternateFileList) alternateFileList->setMinimumRowHeight(usesResponsiveViewport()?44:0);
	if (!usesResponsiveViewport())
	{
		Glob2Screen::paint();
		return;
	}
	const auto safe = mobileDialogSafe(globalContainer->gfx);
	const int w = std::min(920, int(safe.w) - 24), x = int(safe.x) + (int(safe.w) - w) / 2;
	const int top = int(safe.y) + 12, bottom = int(safe.y + safe.h) - 12, footer = bottom - 48;
	const bool wide = w >= 500;
	const int previewSize =
		std::max(64, std::min(wide ? w / 2 - 16 : w / 2,
							  wide ? footer - top - 104 : (footer - top - 64) / 2));
	title->setScreenRectangle(x, top, w, 28);
	const int body = top + 36;
	const int detailX = wide ? x + w / 2 + 12 : x + previewSize + 12;
	const int detailW = wide ? w / 2 - 12 : w - previewSize - 12;
	mapPreview->setScreenRectangle(wide ? detailX : x, body, previewSize, previewSize);
	const int infoY = wide ? body + previewSize + 8 : body;
	int row = infoY;
	for (auto *label : {mapName, mapSize, mapInfo, mapVersion, mapDate})
	{
		label->setScreenRectangle(detailX, row, detailW, 22);
		row += 24;
		label->visible = row < (wide ? footer - 8 : body + previewSize + 8);
	}
	const int listY = wide ? body : body + std::max(previewSize, 48) + 12;
	const int listW = wide ? w / 2 - 12 : w;
	const int toolsY = footer - 52;
	fileList->setScreenRectangle(x, listY, listW, std::max(32, toolsY - listY - 8));
	if (alternateFileList)
		alternateFileList->setScreenRectangle(x, listY, listW, std::max(32, toolsY - listY - 8));
	std::vector<Button *> tools;
	for (auto *button : {static_cast<Button *>(switchType), static_cast<Button *>(importButton),
						 static_cast<Button *>(exportButton), deleteMap})
		if (button && button->visible)
			tools.push_back(button);
	for (size_t i = 0; i < tools.size(); ++i)
		tools[i]->setScreenRectangle(x + int(i) * listW / int(tools.size()), toolsY,
									 listW / int(tools.size()) - 4, 44);
	cancel->setScreenRectangle(x, footer, w / 2 - 4, 48);
	ok->setScreenRectangle(x + w / 2 + 4, footer, w / 2 - 4, 48);
	SDL_Rect bounds{x, top, w, bottom - top};
	if (FrontendTheme::current)
		FrontendTheme::current->background(gfx, true, &bounds);
}
