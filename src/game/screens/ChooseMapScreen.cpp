// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "ChooseMapScreen.h"
#include "FileImport.h"
#include "GUIMapPreview.h"
#include "Game.h"
#include "Team.h"
#include <algorithm>
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
	tilingSource = {};
	tileX = tileY = tileBases = 1;
	tileTeams = 0;
	showTiling = false;
	tilingValid = true;
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
	selectFile(catalog.path(catalog.names()[std::size_t(index)]));
}

void ChooseMapScreen::editMapParameters(const std::string &path)
{
	parametersOnly = true;
	title = tr("Size and parameters");
	selectFile(path);
	showTiling = true;
}

void ChooseMapScreen::selectFile(const std::string &mapFileName)
{
	clearSelection();
	invalidate();
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
			mapHeader.setFileNameOverride(mapFileName);
			if (!parametersOnly)
				mapHeader.setMapName(glob2FilenameToName(mapFileName));
            if (validMapSelected && !mapHeader.requiredTerrainExperiments.empty())
                mapExperiments = tr("[Experiments]") + ": " + experimentLabelList(mapHeader.requiredTerrainExperiments);
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
				if (selectedType == MAP)
				{
					tilingSource = MapTiling::readMapInfo(mapFileName);
					tilingSource.header = mapHeader;
					tilingSource.header.setFileNameOverride(mapFileName);
					tileTeams = mapHeader.getNumberOfTeams();
				}
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
	if (!validMapSelected || !tilingValid || importBusy())
		return;
	if (tilingActive())
	{
		try
		{
			MapHeader written = MapTiling::writeTiledMap(tilingSource.header, tileX, tileY, tileTeams, tileBases);
			if (written.getNumberOfTeams() == 0)
			{
				showStatus(tr("[Map repetition failed]"));
				return;
			}
			mapHeader = std::move(written);
		}
		catch (const std::exception &)
		{
			showStatus(tr("[Map repetition failed]"));
			return;
		}
	}
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


bool ChooseMapScreen::tilingActive() const
{
	return selectedType == MAP && tilingSource.valid &&
		(MapTiling::isActive(tileX, tileY, tileTeams, tilingSource.header.getNumberOfTeams()) ||
		 tileTeams * tileBases < MapTiling::colonyCount(tilingSource.header.getNumberOfTeams(), tileX, tileY));
}

void ChooseMapScreen::refreshTiling()
{
	// Clamp dependent counts before rendering: every player gets an equal
	// share, and reducing a repeat must never leave an impossible selection.
	const int total = MapTiling::colonyCount(tilingSource.header.getNumberOfTeams(), tileX, tileY);
	tileTeams = std::clamp(tileTeams, 1, std::min<int>(Team::MAX_COUNT, total));
	tileBases = std::clamp(tileBases, 1, total / tileTeams);
	tilingValid = true;
	try
	{
		if (tilingActive())
		{
			MapThumbnail thumbnail;
			tilingValid = MapTiling::tiledThumbnail(tilingSource.header, tileX, tileY, tileTeams, tileBases, thumbnail);
			mapPreview->setMapThumbnail(thumbnail);
			mapHeader = MapTiling::tiledHeader(tilingSource.header, tileX, tileY, tileTeams);
		}
		else
		{
			mapHeader = tilingSource.header;
			mapPreview->setMapThumbnail(tilingSource.header.getFileName().c_str());
		}
	}
	catch (const std::exception &)
	{
		tilingValid = false;
		mapPreview->setMapThumbnail(MapThumbnail());
	}
	updateMapInformation();
	if (tilingActive())
	{
		mapSize = GAGCore::FormattableString("%0 x %1 (%2 x %3)")
			.arg(tilingSource.w * tileX).arg(tilingSource.h * tileY).arg(tilingSource.w).arg(tilingSource.h);
		mapInfo += " / " + GAGCore::FormattableString("%0 %1")
			.arg(MapTiling::placedColonyCount(total, tileTeams, tileBases)).arg(tr("[bases]"));
	}
	status = tilingValid ? title : tr("[Map repetition failed]");
	invalidate();
}

Element ChooseMapScreen::tilingControls(const Presentation &p, bool busy)
{
	if (!validMapSelected || selectedType != MAP || !tilingSource.valid)
		return empty();
	ButtonOptions disclosure;
	disclosure.selected = showTiling;
	disclosure.enabled = !busy;
	disclosure.flat = true;
	disclosure.alignLeft = true;
	std::vector<Element> controls;
	if (!parametersOnly)
		controls.push_back(button("tiling/parameters", tr("Size and parameters"),
			[this] { showTiling = !showTiling; invalidate(); }, disclosure));
	if (!showTiling)
		return column(std::move(controls));
	auto repeat = [this, busy](const char *key, const char *labelKey, int side, int current, bool horizontal)
	{
		const auto values = MapTiling::repeatOptions(side);
		std::vector<std::string> labels;
		for (int value : values)
			labels.push_back(std::to_string(value));
		ChoiceOptions options;
		options.controlEnabled = !busy;
		return field(tr(labelKey), choice(key, labels, int(std::find(values.begin(), values.end(), current) - values.begin()),
			[this, values, horizontal](int index)
			{
				(horizontal ? tileX : tileY) = values.at(std::size_t(index));
				// A new repeat defaults to one player per base, up to the engine limit.
				const int total = MapTiling::colonyCount(tilingSource.header.getNumberOfTeams(), tileX, tileY);
				tileTeams = std::min<int>(Team::MAX_COUNT, total);
				tileBases = total / tileTeams;
				refreshTiling();
			}, options));
	};
	const int total = MapTiling::colonyCount(tilingSource.header.getNumberOfTeams(), tileX, tileY);
	controls.push_back(field(tr("[colonies]"), stepper("tiling/teams", tileTeams, 1, std::min<int>(Team::MAX_COUNT, total),
		[this](int value) { tileTeams = value; tileBases = 1; refreshTiling(); }, {1, !busy})));
	controls.push_back(paragraph(tr("[bases per colony]"), {FontRole::Support, true}));
	controls.push_back(field(tr("[bases]"), stepper("tiling/bases", tileBases, 1, total / tileTeams,
		[this](int value) { tileBases = value; refreshTiling(); }, {1, !busy})));
	// Ordinary colony settings come first; repeating terrain is an opt-in
	// advanced adjustment at the bottom, starting at one in both directions.
	controls.push_back(repeat("tiling/x", "[repeat map horizontally]", tilingSource.w, tileX, true));
	controls.push_back(repeat("tiling/y", "[repeat map vertically]", tilingSource.h, tileY, false));
	controls.push_back(button("tiling/reset", tr("Reset"), [this]
		{
			tileX = tileY = tileBases = 1;
			tileTeams = tilingSource.header.getNumberOfTeams();
			refreshTiling();
		}, {false, false, !busy}));
	return column(std::move(controls), {p.pt(8)});
}

Element ChooseMapScreen::build(const Presentation &p)
{
	auto &catalog = activeCatalog();
	const bool busy = importBusy();
	auto list = listView("files", catalog.names(), activeSelection(), [this](int i) { select(i); },
						 {{}, {}, {}, [this](int) { accept(); }, {}, 10, tr("[No items]")});
	std::vector<Element> detailLines;
	const auto lines = parametersOnly ? std::vector<std::string>{mapName, mapSize, mapInfo}
		: std::vector<std::string>{mapName, mapSize, mapInfo, mapVersion, mapDate, mapExperiments};
	for (const auto &line : lines)
		if (!line.empty())
			detailLines.push_back(paragraph(line));
	auto preview = Glob2UI::mapPreview("preview", *this->mapPreview, parametersOnly && p.touch ? 120 : 160);
	auto controls = tilingControls(p, busy);
	if (!parametersOnly)
		detailLines.push_back(controls);
	auto details = column(std::move(detailLines), {p.pt(4)});
	std::vector<Element> tools;
	if (alternate)
		tools.push_back(button("switch", loadableTypeName(showingAlternate ? type1 : type2), [this] { switchType(); }, {false, false, !busy}));
	if (canImport)
		tools.push_back(button("import", tr(fileImport && fileImport->canRetry() ? "[retry save]" : "[import file]"), [this] { beginImport(); }, {false, false, !busy}));
	if (canExport)
		tools.push_back(button("export", tr("[export file]"), [this] { exportSelected(); }, {false, false, !busy && (validMapSelected || (fileImport && fileImport->canRetry()))}));
	if (shareMap && activeType() == MAP)
		tools.push_back(button("share", tr("[maps share online]"), [this] { shareMap(tilingSource.valid ? tilingSource.header.getFileName() : mapHeader.getFileName()); },
							   {false, false, !busy && validMapSelected}));
	if (canDelete)
		tools.push_back(button("delete", tr("[delete]"), [this] { deleteSelected(); }, {false, false, !busy && validMapSelected, false, false, true}));
	auto toolRow = tools.empty() ? empty() : wrap(std::move(tools), {-1, p.pt(140)});
	auto statusLine = status == title ? empty() : paragraph(status, {FontRole::Support, true});
	Element body = adaptive(
		[list, preview, details, toolRow, statusLine](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(640))
				return scroll("choose/scroll", column({statusLine, column({align(Alignment::TopLeft, preview), details}), list, toolRow}));
			auto side = scroll("choose/details", column({align(Alignment::TopLeft, preview), details, statusLine}));
			return column({expanded(row({width(ctx.presentation.pt(300), column({expanded(list), toolRow})), expanded(side)}, {-1, CrossAlign::Stretch}))});
		});
	if (parametersOnly)
		body = adaptive([preview, details, controls, statusLine](const LayoutContext &ctx, Size available)
		{
			if (available.w < ctx.presentation.pt(640))
				return scroll("choose/parameters", column({statusLine,
					row({preview, expanded(details)}, {ctx.presentation.pt(12), CrossAlign::Start}), controls},
					{ctx.presentation.pt(12)}));
			return row({width(ctx.presentation.pt(240), scroll("choose/source", column({preview, details}))),
				expanded(scroll("choose/parameters", column({statusLine, controls})))},
				{ctx.presentation.pt(20), CrossAlign::Start});
		});
	return page(title, body,
				actions({{"ok", tr("[ok]"), [this] { accept(); }, true, SDLK_RETURN, validMapSelected && tilingValid && !busy},
						 {"cancel", tr("[Cancel]"), [this] { cancel(); }, false, SDLK_ESCAPE}},
						p),
				p, 960);
}
