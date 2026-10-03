// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CustomGameSetup.h"
#include "MapHeader.h"
#include "StartQuality.h"
#include "ui/FrontendUI.h"
#include <ScreenStack.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
class LandscapePickerScreen;
class LandscapePreviewer;
class LobbyMapPreview;

// A titled list of choices with a description for the selected one (AI profiles).
class CustomGameChoiceScreen : public Glob2UI::Screen
{
	friend struct CustomGameSetupHarness;
	std::string title;
	std::vector<std::string> choices;
	int selected;
	bool profiles;
	std::vector<bool> enabled;

  public:
	const char *recordingId() const override { return "custom_game_choice"; }
	CustomGameChoiceScreen(const std::string &, const std::vector<std::string> &, int, bool, const std::vector<bool> &);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void choose(int index) { selected = index; invalidate(); }
	void use() { endExecute(selected); }

  protected:
	void onEscape() override { endExecute(-2); }
};

class CustomGameScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "custom_game"; }
	enum
	{
		OK = 1,
		CANCEL = 2
	};
	explicit CustomGameScreen(GAGGUI::ScreenStack &screens);
	~CustomGameScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	void onTimer(Uint32 tick) override;
	MapHeader &getMapHeader() { return mapHeader; }
	GameHeader &getGameHeader();
	int getSelectedColor(int) { return setup.humanColony().value_or(0); }
	// A premade map's file path; empty for a generated map (see generatedSnapshot).
	const std::string &sourceFile() const { return source; }
	// Hands over the generated map's serialized bytes (null for a premade map),
	// which the loader reads directly instead of this screen ever holding them.
	std::shared_ptr<std::string> releaseSnapshot();
	void launchFailed()
	{
		message = "Could not launch this map. Your setup is retained; try again.";
		invalidate();
	}
	int selectedSpeed() const { return setup.speed; }
	// Room setup (RoomScreen): edit an online room's map and rules with this screen.
	// Start becomes "Use in room" and ends with OK without generating or launching;
	// the room's server generates the map.
	void useForRoom(const CustomGameSetup &draft, int tab);
	bool roomMode() const { return forRoom; }
	const CustomGameSetup &draft() const { return setup; }
	// Semantic entry points shared with the harnesses.
	void selectTab(int tab);
	int tab() const { return currentTab; }
	void launch();

  protected:
	void onEscape() override { endExecute(CANCEL); }
	bool interceptEvent(const SDL_Event &event) override;

  private:
	friend struct MapPreviewHarness;
	friend struct MobileGallerySetup;
	friend struct CustomGameSetupHarness;
	GAGGUI::ScreenStack &screens;
	CustomGameSetup setup;
	MapHeader mapHeader;
	GameHeader gameHeader;
	std::string username, source, message;
	// Serialized bytes of the last successfully generated map (see generateMap()), read
	// directly by the loader instead of round-tripping through a temporary file.
	std::shared_ptr<std::string> generatedSnapshot;
	std::string lastSavedPreferences;
	Uint32 preferencesRetryAt = 0;
	void savePreferences();
	unsigned previewRevision = ~0u;
	bool previewPending = false;
	Uint32 previewDue = 0;
	// A seed the landscape picker showed a map for: the next preview reproduces that map.
	std::optional<std::uint32_t> chosenSeed;
	// How good a start each colony got on the map in the preview, as GenerationService scored
	// it; measured only while the preview is a generated map.
	MapGeneration::StartQualityReport quality;
	// Random parameters (see randomizeParameters): draws still allowed when the world refuses a
	// set, so the preview keeps redrawing until it shows a map.
	int randomAttempts = 0;
	static constexpr int kRandomAttempts = 6;
	// The preview's candidate rolls run on the previewer's workers; the best-scoring one is then
	// rolled again on this thread for the snapshot. candidateRevision is the draft they were
	// rolled for, so an edit made meanwhile discards them.
	std::unique_ptr<LandscapePreviewer> candidates;
	unsigned candidateRevision = 0;
	void startCandidates();
	bool collectCandidates();
	void finishPreview();
	bool previewBusy() const { return previewPending || candidates != nullptr; }
	bool validMap = false, userMaps = false;
	// LandscapePickerScreen::SortOrder, kept as a plain int here so this header does not need
	// that screen's full declaration; see CustomGamePreferences::landscapeSortOrder.
	int landscapeSortOrder = 0;
	bool separateMapLibraries = true;
	int currentTab = 0;
	bool forRoom = false;
	std::unique_ptr<LobbyMapPreview> preview;
	std::vector<std::string> mapPaths, mapNames;
	std::string librarySelection[2];
	bool expanded[3] = {false, false, false};
	Glob2UI::Element mapTab(const Glob2UI::Presentation &p, bool narrow);
	Glob2UI::Element playersTab(const Glob2UI::Presentation &p, bool narrow);
	// One colony's identity, controller, AI and team controls, built fresh for
	// whichever arrangement the offered width allows.
	struct ColonyFields
	{
		Glob2UI::Element identity, controller, ai, team;
	};
	ColonyFields colonyFields(int colony, const Glob2UI::Presentation &p);
	Glob2UI::Element rulesTab(const Glob2UI::Presentation &p, bool narrow);
	Glob2UI::Element ruleControl(int index, const Glob2UI::Presentation &p, std::string &help);
	void setMapMode(bool random);
	void showAIProfile(int colony);
	void listMaps();
	bool loadMap(const std::string &path);
	bool generateMap();
	std::vector<std::pair<int, GenerationRequest>> landscapeEntries() const;
	LandscapePickerScreen *chooseLandscape();
	void applyLandscape(int method, std::optional<std::uint32_t> seed, const GenerationRequest *shown = nullptr);
	void resetParameters();
	void randomizeParameters();
	bool drawRandomParameters();
	void showStartQuality();
	// Discard the current preview and schedule a new one after the edit settles.
	void invalidatePreview();
	std::string colonyLabel(int) const;
};
