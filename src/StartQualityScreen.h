// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Glob2Screen.h"
#include "StartQuality.h"
#include <string>
#include <vector>
class LobbyControls;

/// The breakdown behind a generated map's start quality (FEEDBACK 2026-09-14: "a little (i) icon
/// that i can click on which shows me the breakdown of all the different things analyzed which
/// went into the fairness score"): one row per colony with what was measured on the finished map
/// and how each factor scored, then the totals the lobby ranks its candidate rolls by. Read-only;
/// Back or Escape returns.
class StartQualityScreen : public Glob2Screen
{
	friend struct CustomGameSetupHarness;

  public:
	enum
	{
		BACK = -2
	};
	StartQualityScreen(const MapGeneration::StartQualityReport &report,
					   std::vector<std::string> colonyLabels, std::vector<Color> colonyColors);
	~StartQualityScreen() override;
	void onAction(Widget *, Action, int, int) override;
	void onSDLEvent(SDL_Event *) override;

  private:
	void render();
	MapGeneration::StartQualityReport report;
	std::vector<std::string> labels;
	std::vector<Color> colors;
	LobbyControls *controls;
};
