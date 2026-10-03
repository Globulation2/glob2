// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "ChooseMapScreen.h"
#include "FileImport.h"
#include "GUIMapPreview.h"
#include "Game.h"
#include "GlobalContainer.h"
#include <ApplicationHost.h>
#include <BinaryStream.h>
#include <FileManager.h>
#include <FormatableString.h>
#include <Stream.h>
#include <Toolkit.h>
#include <ctime>
#include <iostream>
#include <memory>

using namespace Glob2UI;

ChooseMapScreen::ChooseMapScreen(const char *directory, const char *extension, bool,
								 const char *alternateDirectory, const char *alternateExtension, bool)
	: primary(directory, extension)
{
	mapPreview = std::make_unique<MapPreview>();
	if (std::string(directory) == "maps")
	{
		type1 = MAP;
		type2 = GAME;
		title = tr("[choose map]");
	}
	else if (std::string(directory) == "games")
	{
		type1 = GAME;
		type2 = REPLAY;
		title = tr("[choose game]");
		canDelete = true;
		canExport = GAGCore::ApplicationHost::canExportFiles();
	}
	else
	{
		type1 = GAME;
		type2 = NONE;
		title = tr("[choose game]");
	}
	if (alternateDirectory)
	{
		assert(type2 != NONE);
		alternate.emplace(alternateDirectory, alternateExtension ? alternateExtension : "");
	}
	if (GAGCore::ApplicationHost::canImportFiles())
	{
		canImport = true;
		canExport = true;
	}
	status = title;
}

ChooseMapScreen::~ChooseMapScreen() = default;

FileCatalog &ChooseMapScreen::activeCatalog()
{
	return showingAlternate && alternate ? *alternate : primary;
}

bool ChooseMapScreen::importBusy() const
{
	return fileSelection || (fileImport && (fileImport->state() == FileImport::State::Validating ||
											fileImport->state() == FileImport::State::Persisting));
}

void ChooseMapScreen::showStatus(const std::string &text)
{
	status = text;
	invalidate();
}

void ChooseMapScreen::clearSelection()
{
	validMapSelected = false;
	selectedType = NONE;
	mapDate.clear();
	mapExperiments.clear();
	mapVersion.clear();
	mapInfo.clear();
	mapSize.clear();
	mapName.clear();
	mapPreview->setMapThumbnail(MapThumbnail());
	status = title;
}

bool ChooseMapScreen::selectNamed(const std::string &name)
{
	const int index = activeCatalog().indexOf(name);
	if (index < 0)
		return false;
	select(index);
	return true;
}

bool ChooseMapScreen::hasPreview() const
{
	return mapPreview && mapPreview->isThumbnailLoaded();
}

void ChooseMapScreen::select(int index)
{
	auto &catalog = activeCatalog();
	activeSelection() = index;
	// Invalidate the old selection before attempting any fallible file reads.
	clearSelection();
	invalidate();
	if (index < 0 || index >= int(catalog.names().size()))
		return;
	const std::string mapFileName = catalog.path(catalog.names()[std::size_t(index)]);
	try
	{
		mapPreview->setMapThumbnail(mapFileName.c_str());
		auto stream = std::unique_ptr<GAGCore::InputStream>(new GAGCore::BinaryInputStream(
			GAGCore::Toolkit::getFileManager()->openInflatingInputStreamBackend(mapFileName)));
		if (stream->isEndOfStream())
			std::cerr << "ChooseMapScreen: can't open file " << mapFileName << std::endl;
		else
		{
			validMapSelected = mapHeader.load(stream.get());
			if (!validMapSelected)
				selectedType = NONE;
			mapHeader.setMapName(glob2FilenameToName(mapFileName));
			if (validMapSelected && activeType() == GAME)
			{
				// A save keeps the experiments it was started with, whatever the
				// settings say now, so name them. A header that will not read is
				// the loader's to report, not the listing's.
				try
				{
					GameHeader saved;
					if (saved.load(stream.get(), mapHeader.getVersionMinor()) && !saved.getExperiments().empty())
						mapExperiments = tr("[Experiments]") + ": " + experimentLabelList(saved.getExperiments());
				}
				catch (std::exception &)
				{
				}
			}
			if (validMapSelected)
			{
				updateMapInformation();
				time_t mtime = GAGCore::Toolkit::getFileManager()->mtime(mapFileName);
				mapDate = ctime(&mtime);
				while (!mapDate.empty() && (mapDate.back() == '\n' || mapDate.back() == '\r'))
					mapDate.pop_back();
				selectedType = activeType();
			}
			else
				std::cerr << "ChooseMapScreen: invalid map header for map " << mapFileName << std::endl;
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
		status = tr("[Damaged Map]");
	}
}

void ChooseMapScreen::updateMapInformation()
{
	mapName = mapHeader.getMapName();
	mapInfo = GAGCore::FormattableString("%0%1").arg(mapHeader.getNumberOfTeams()).arg(tr("[teams]"));
	mapVersion = GAGCore::FormattableString("%0 %1.%2")
					 .arg(tr("[Version]"))
					 .arg(mapHeader.getVersionMajor())
					 .arg(mapHeader.getVersionMinor());
	mapSize = GAGCore::FormattableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
	validMapSelectedhandler();
}

MapHeader &ChooseMapScreen::getMapHeader() { return mapHeader; }
GameHeader &ChooseMapScreen::getGameHeader() { return gameHeader; }
ChooseMapScreen::LoadableType ChooseMapScreen::getSelectedType() { return selectedType; }

std::string ChooseMapScreen::loadableTypeName(LoadableType type)
{
	switch (type)
	{
	case GAME:
		return tr("[the games]");
	case MAP:
		return tr("[the maps]");
	case REPLAY:
		return tr("[the replays]");
	case NONE:
		break;
	}
	assert(false);
	return {};
}

void ChooseMapScreen::switchType()
{
	if (!alternate)
		return;
	showingAlternate = !showingAlternate;
	select(activeSelection());
}

void ChooseMapScreen::accept()
{
	if (validMapSelected)
		endExecute(OK);
}

void ChooseMapScreen::cancel()
{
	if (importBusy())
	{
		if (!fileImport || fileImport->state() != FileImport::State::Persisting)
		{
			fileSelection.reset();
			fileImport.reset();
			GAGCore::ApplicationHost::importChanged("cancelled");
			showStatus(tr("[import cancelled]"));
		}
		return;
	}
	endExecute(CANCEL);
}

void ChooseMapScreen::deleteSelected()
{
	auto &catalog = activeCatalog();
	const int index = activeSelection();
	if (index < 0 || index >= int(catalog.names().size()))
		return;
	GAGCore::Toolkit::getFileManager()->remove(catalog.path(catalog.names()[std::size_t(index)]));
	catalog.refresh();
	const int next = catalog.names().empty() ? -1 : std::min(index, int(catalog.names().size()) - 1);
	select(next);
}

void ChooseMapScreen::exportSelected()
{
	if (fileImport && fileImport->canRetry())
	{
		if (!fileImport->exportFile())
			showStatus(tr("[export failed]"));
		return;
	}
	auto &catalog = activeCatalog();
	const int index = activeSelection();
	if (index < 0 || index >= int(catalog.names().size()))
		return;
	if (!GAGCore::ApplicationHost::exportLocalFile(catalog.path(catalog.names()[std::size_t(index)])))
		showStatus(tr("[export failed]"));
}

void ChooseMapScreen::beginImport()
{
	if (importBusy())
		return;
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
	showStatus(tr("[select import file]"));
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
	showStatus(tr(message));
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
				fileImport = std::make_unique<FileImport>(fileSelection->takeFile(), importExtension);
			}
			catch (const std::exception &)
			{
				showStatus(tr("[import failed]"));
			}
		}
		else
		{
			const bool cancelled = state == GAGCore::ApplicationHost::FileSelectionState::Cancelled;
			GAGCore::ApplicationHost::importChanged(cancelled ? "cancelled" : "invalid");
			showStatus(tr(cancelled ? "[import cancelled]" : "[import failed]"));
		}
		fileSelection.reset();
	}
	if (!fileImport)
		return;
	fileImport->advance();
	updateImportStatus();
	if (fileImport->state() == FileImport::State::Succeeded)
	{
		auto &catalog = activeCatalog();
		catalog.refresh();
		select(catalog.indexOf(glob2FilenameToName(fileImport->path())));
		fileImport.reset();
	}
}

Element ChooseMapScreen::build(const Presentation &p)
{
	auto &catalog = activeCatalog();
	const bool busy = importBusy();
	auto list = listView("files", catalog.names(), activeSelection(), [this](int i) { select(i); },
						 {{}, {}, {}, [this](int) { accept(); }, {}, 10, tr("[No items]")});
	std::vector<Element> detailLines;
	for (const auto &line : {mapName, mapSize, mapInfo, mapVersion, mapDate, mapExperiments})
		if (!line.empty())
			detailLines.push_back(paragraph(line));
	auto preview = Glob2UI::mapPreview("preview", *this->mapPreview, 160);
	auto details = column(std::move(detailLines), {p.pt(4)});
	std::vector<Element> tools;
	if (alternate)
		tools.push_back(button("switch", loadableTypeName(showingAlternate ? type1 : type2), [this] { switchType(); }, {false, false, !busy}));
	if (canImport)
		tools.push_back(button("import", tr(fileImport && fileImport->canRetry() ? "[retry save]" : "[import file]"), [this] { beginImport(); }, {false, false, !busy}));
	if (canExport)
		tools.push_back(button("export", tr("[export file]"), [this] { exportSelected(); }, {false, false, !busy && (validMapSelected || (fileImport && fileImport->canRetry()))}));
	if (shareMap && activeType() == MAP)
		tools.push_back(button("share", tr("[maps share online]"), [this] { shareMap(mapHeader.getFileName()); },
							   {false, false, !busy && validMapSelected}));
	if (canDelete)
		tools.push_back(button("delete", tr("[delete]"), [this] { deleteSelected(); }, {false, false, !busy && validMapSelected, false, false, true}));
	auto toolRow = tools.empty() ? empty() : wrap(std::move(tools), {-1, p.pt(140)});
	auto statusLine = status == title ? empty() : paragraph(status, {FontRole::Support, true});
	Element body = adaptive(
		[list, preview, details, toolRow, statusLine](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(640))
				return scroll("choose/scroll", column({statusLine, row({preview, expanded(details)}, {-1, CrossAlign::Start}), list, toolRow}));
			auto side = column({align(Alignment::TopLeft, preview), details, statusLine});
			return column({expanded(row({width(ctx.presentation.pt(300), column({expanded(list), toolRow})), expanded(side)}, {-1, CrossAlign::Stretch}))});
		});
	return page(title, body,
				actions({{"ok", tr("[ok]"), [this] { accept(); }, true, SDLK_RETURN, validMapSelected && !busy},
						 {"cancel", tr("[Cancel]"), [this] { cancel(); }, false, SDLK_ESCAPE}},
						p),
				p, 960);
}
