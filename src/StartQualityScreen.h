// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StartQuality.h"
#include "ui/FrontendUI.h"
#include <set>
#include <string>
#include <vector>

/// The breakdown behind a generated map's start quality: one row per colony with what was
/// measured on the finished map and how each factor scored, then the totals the lobby ranks
/// its candidate rolls by. Read-only; Back or Escape returns.
class StartQualityScreen : public Glob2UI::Screen
{
  public:
    const char* recordingId() const override { return "start_quality"; }
	enum
	{
		BACK = -2
	};
	StartQualityScreen(const MapGeneration::StartQualityReport &report, std::vector<std::string> colonyLabels,
					   std::vector<GAGCore::Color> colonyColors);
	~StartQualityScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

  protected:
	void onEscape() override { endExecute(BACK); }

  private:
	friend struct MobileGallerySetup;
	friend struct CustomGameSetupHarness;
	Glob2UI::Element table(const Glob2UI::Presentation &presentation);
	Glob2UI::Element cards(const Glob2UI::Presentation &presentation);
	std::set<std::size_t> expanded;
	MapGeneration::StartQualityReport report;
	std::vector<std::string> labels;
	std::vector<GAGCore::Color> colors;
};
