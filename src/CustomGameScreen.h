// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CustomGameSetup.h"
#include "Glob2Screen.h"
#include "MapHeader.h"
#include "StartQuality.h"
#include <cstdint>
#include <ScreenStack.h>
#include <memory>
#include <optional>
#include <vector>
class LobbyControls;
class LandscapePickerScreen;
class LandscapePreviewer;
class CustomGameChoiceScreen : public Glob2Screen
{
	friend struct CustomGameSetupHarness;
	friend struct MobilePresentationHarness;
	LobbyControls *controls;
	std::string title;
	std::vector<std::string> choices;
	int selected;
	bool profiles;
	std::vector<bool> enabled;

  public:
    bool usesResponsiveViewport() const override;
    void cancelExecutionInput() override;
	CustomGameChoiceScreen(const std::string &, const std::vector<std::string> &, int, bool,
						   const std::vector<bool> &);
	void onSDLEvent(SDL_Event *) override;
	void onAction(Widget *, Action, int, int) override;
};
class LobbyMapPreview;
class LobbyControls;
class CustomGameScreen : public Glob2TabScreen
{
  public:
	void paint() override;
    bool usesResponsiveViewport() const override;
    friend struct MobilePresentationHarness;
    void cancelExecutionInput() override;
	enum
	{
		OK = 1,
		CANCEL = 2
	};
	explicit CustomGameScreen(GAGGUI::ScreenStack& screens);
	~CustomGameScreen() override;
	void onAction(Widget *, Action, int, int) override;
	void onGroupActivated(int) override;
	void onSDLEvent(SDL_Event *) override;
	void onTimer(Uint32 tick) override;
	void updateLayout() override;
	MapHeader &getMapHeader() { return mapHeader; }
	GameHeader &getGameHeader();
	int getSelectedColor(int) { return setup.humanColony().value_or(0); }
	const std::string &sourceFile() const { return source; }
	// Hands over a generated map, which this screen would otherwise delete when
	// destroyed. Releasing the returned owner removes it.
	std::shared_ptr<void> releaseSnapshot();
	void launchFailed()
	{
		message = "Could not launch this map. Your setup is retained; try again.";
	}
	int selectedSpeed() const { return setup.speed; }

  private:
	friend struct MapPreviewHarness;
	friend struct CustomGameSetupHarness;
	GAGGUI::ScreenStack& screens;
	CustomGameSetup setup;
	MapHeader mapHeader;
	GameHeader gameHeader;
	std::string username, source, snapshot, message;
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
	int groups[3];
	LobbyMapPreview *preview;
	LobbyControls *controls;
	std::vector<std::string> mapPaths, mapNames;
	std::string librarySelection[2];
	bool expanded[3] = {false, false, false};
	void renderLobby();
    void renderPhoneLobby();
	void renderPlayers(int x, int y, int w, int h);
	void renderRules(int x, int y, int w, int h);
	void renderMap(int x, int y, int w, int h);
	void setMapMode(bool random);
	void showAIProfile(int colony);
	void listMaps();
	bool loadMap(const std::string &path);
	bool generateMap();
	std::vector<std::pair<int, GenerationRequest>> landscapeEntries() const;
	void chooseLandscape();
	void applyLandscape(int method, std::optional<std::uint32_t> seed,
						const GenerationRequest *shown = nullptr);
	void resetParameters();
	void randomizeParameters();
	bool drawRandomParameters();
	void showStartQuality();
	void invalidate();
	std::string colonyLabel(int) const;
};
