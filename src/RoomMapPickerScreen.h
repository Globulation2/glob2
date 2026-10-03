// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#pragma once
#include "CustomGameSetup.h"
#include "LandscapePreviewer.h"
#include "ui/FrontendUI.h"
#include <memory>
#include <string>
#include <vector>

class MapPreview;

// "Change map…" in an online room: a simple first view over the room instead of the
// whole custom-game screen. Generated (fair landscapes at a size and colony count,
// each with its preview), Premade (the maps shipped with the game, with players and
// size) and Catalog (maps players shared, in the map browser). "More options…" opens
// the full custom-game screen for everything else.
class RoomMapPickerScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "room_map_picker"; }
	enum Result
	{
		Cancelled = 0,
		Chosen = 1,
		MoreOptions = 2,
		Catalog = 3
	};
	enum Tab
	{
		GeneratedTab,
		PremadeTab,
		CatalogTab
	};
	// `people`: everyone in the room, which sets the suggested colony count. `current`
	// is the room's draft: its rules and teams stay; only the map changes.
	RoomMapPickerScreen(int people, const CustomGameSetup &current);
	~RoomMapPickerScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;

	// Harness entry points.
	void selectTab(int tab);
	void selectGenerated(int index);
	void selectPremade(int index);
	void setSize(int index);
	void setColonies(int colonies);
	int tab() const { return currentTab; }
	// Captures and harnesses wait for these.
	bool previewsBusy() const { return previewer && previewer->busy(); }
	bool premadesLoaded() const { return nextPremade >= premades.size(); }

	// After Chosen: the room's draft with the new generated map, or the premade map.
	bool choseGenerated() const { return currentTab == GeneratedTab; }
	CustomGameSetup generatedSetup() const;
	std::string premadePath() const;
	std::string premadeTitle() const;
	// The map ready to use (its preview generated, or the premade map read).
	bool canUse() const;

  protected:
	void onEscape() override { endExecute(Cancelled); }

  private:
	struct Landscape
	{
		std::string id, name;
		int method = -1;
		std::unique_ptr<MapPreview> preview;
	};
	struct Premade
	{
		std::string path, name;
		int players = 0, width = 0, height = 0;
		bool loaded = false, failed = false;
		std::unique_ptr<MapPreview> preview;
	};
	CustomGameSetup base;
	int people;
	int currentTab = GeneratedTab;
	int sizeIndex = 1; // 64, 128, 256
	int colonies = 2;
	int generatedIndex = 0, premadeIndex = -1;
	std::vector<Landscape> landscapes;
	std::vector<Premade> premades;
	std::unique_ptr<LandscapePreviewer> previewer;
	std::vector<unsigned> seenRevisions;
	std::size_t nextPremade = 0;

	std::vector<GenerationRequest> requests() const;
	void restartPreviews();
	void refreshPreviews();
	void loadSomePremades();
	Glob2UI::Element generatedTab(const Glob2UI::Presentation &p);
	Glob2UI::Element premadeTab(const Glob2UI::Presentation &p);
	Glob2UI::Element catalogTab(const Glob2UI::Presentation &p);
};
