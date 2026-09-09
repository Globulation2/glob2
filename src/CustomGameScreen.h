// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CustomGameSetup.h"
#include "Glob2Screen.h"
#include "MapHeader.h"
#include <memory>
#include <vector>
class LobbyControls;
class CustomGameChoiceScreen : public Glob2Screen
{
	friend struct CustomGameSetupHarness;
	LobbyControls *controls;
	std::string title;
	std::vector<std::string> choices;
	int selected;
	bool profiles;
	std::vector<bool> enabled;

  public:
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
	enum
	{
		OK = 1,
		CANCEL = 2
	};
	CustomGameScreen();
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
	void launchFailed()
	{
		message = "Could not launch this map. Your setup is retained; try again.";
	}
	int selectedSpeed() const { return setup.speed; }

  private:
	friend struct CustomGameSetupHarness;
	CustomGameSetup setup;
	MapHeader mapHeader;
	GameHeader gameHeader;
	std::string username, source, snapshot, message;
	unsigned previewRevision = ~0u;
	bool previewPending = false;
	Uint32 previewDue = 0;
	bool validMap = false, userMaps = false;
	int currentTab = 0;
	int groups[3];
	LobbyMapPreview *preview;
	LobbyControls *controls;
	std::vector<std::string> mapPaths, mapNames;
	std::string librarySelection[2];
	bool expanded[3] = {false, false, false};
	void renderLobby();
	void renderPlayers(int x, int y, int w, int h);
	void renderRules(int x, int y, int w, int h);
	void renderMap(int x, int y, int w, int h);
	void setMapMode(bool random);
	void showAIProfile(int colony);
	void listMaps();
	bool loadMap(const std::string &path);
	bool generateMap();
	int choose(const std::string &, const std::vector<std::string> &, int, bool profiles = false,
			   const std::vector<bool> &enabled = {});
	void invalidate();
	std::string colonyLabel(int) const;
};
