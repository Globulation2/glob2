// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "GameHeader.h"
#include "MapHeader.h"
#include "MapTiling.h"
#include "ui/FileListing.h"
#include "ui/FrontendUI.h"
#include <ApplicationHost.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class FileImport;
class MapPreview;

//! Selects a map, a saved game or a replay, optionally switching between two directories.
class ChooseMapScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "choose_map"; }
	/// Directory is the source of the listed files and extension the file
	/// extension to show; an alternate directory adds a switch button.
	ChooseMapScreen(const char *directory, const char *extension, bool recurse,
					const char *alternateDirectory = nullptr, const char *alternateExtension = nullptr,
					bool alternateRecurse = false);
	~ChooseMapScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32) override;

	/// The map header of the currently selected map
	MapHeader &getMapHeader();
	/// The game header, with all customized options, for the selected map
	GameHeader &getGameHeader();

	enum
	{
		//! A valid map/game is selected
		OK = 1,
		//! The selection was cancelled
		CANCEL = 2,
		//! Kept for callers; deletion now happens inside the screen
		DELETEGAME = 3,
		SWITCHTYPE = 4,
	};

	enum LoadableType
	{
		NONE,
		GAME,
		MAP,
		REPLAY
	};

	/// The type of the currently selected loadable (NONE, GAME, MAP or REPLAY)
	LoadableType getSelectedType();
	/// Selects the listed file with this display name; false when it is not listed.
	bool selectNamed(const std::string &name);
	/// Whether the preview shows the selected map.
	bool hasPreview() const;

	void select(int index);
	// A source-only advanced dialog; no second file picker or import tools.
	void editMapParameters(const std::string &path);
	/// Adds "Share online…" for maps: called with the selected map's file.
	void enableSharing(std::function<void(const std::string &)> share) { shareMap = std::move(share); invalidate(); }

  protected:
	void onEscape() override { endExecute(CANCEL); }
	/// Called when a valid map has been selected; overridable by subclasses.
	virtual void validMapSelectedhandler() {}

	/// The map header of the currently selected map
	MapHeader mapHeader;
	/// The game header of the currently selected map
	GameHeader gameHeader;

  private:
	// Repetition is opt-in and resets with the source selection. Hiding the
	// section keeps the current preview; resetting restores the authored map.
	MapTiling::MapInfo tilingSource;
	int tileX = 1, tileY = 1, tileTeams = 0, tileBases = 1;
	bool showTiling = false, tilingValid = true, parametersOnly = false;
	void selectFile(const std::string &path);
	bool tilingActive() const;
	void refreshTiling();
	Glob2UI::Element tilingControls(const Glob2UI::Presentation &p, bool busy);
	bool importBusy() const;
	Glob2UI::FileCatalog primary;
	std::optional<Glob2UI::FileCatalog> alternate;
	bool showingAlternate = false;
	int selection[2] = {-1, -1};
	LoadableType type1, type2;
	LoadableType selectedType = NONE;
	bool validMapSelected = false;
	std::string title, status;
	std::string mapName, mapInfo, mapVersion, mapSize, mapDate;
	// The experiments a selected save carries; empty for maps, replays and plain saves.
	std::string mapExperiments;
	std::unique_ptr<MapPreview> mapPreview;
	std::unique_ptr<GAGCore::ApplicationHost::FileSelection> fileSelection;
	std::unique_ptr<FileImport> fileImport;
	std::string importExtension;
	bool canImport = false, canExport = false, canDelete = false;
	std::function<void(const std::string &)> shareMap;

	Glob2UI::FileCatalog &activeCatalog();
	int &activeSelection() { return selection[showingAlternate ? 1 : 0]; }
	LoadableType activeType() const { return showingAlternate ? type2 : type1; }
	static std::string loadableTypeName(LoadableType type);
	void showStatus(const std::string &text);
	void clearSelection();
	void updateMapInformation();
	void updateImportStatus();
	void beginImport();
	void exportSelected();
	void deleteSelected();
	void switchType();
	void accept();
	void cancel();
};
