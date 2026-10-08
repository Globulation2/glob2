// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "PlatformClient.h"
#include "BuildingLibrary.h"
// Accept only IDs or links on the selected instance; never turn pasted URLs into fetch targets.
std::string buildingFamilyIdFromLink(const std::string &input, const std::string &origin);
class BuildingLibraryScreen : public Glob2UI::Screen
{
	friend struct BuildingLibraryScreenHarness;

  public:
	const char *recordingId() const override { return "building_library"; }
	BuildingLibraryScreen();
	Glob2UI::Element build(const Glob2UI::Presentation &) override;
	void onTimer(Uint32) override;

  private:
	BuildingLibrary library;
	Online::PlatformScope calls;
	nlohmann::json families = nlohmann::json::array();
	nlohmann::json openedFamily;
	std::string status, cursor, familyInput;
	bool started = false, busy = false;
	void reload(bool more = false);
	void openFamily();
	void install(const nlohmann::json &family, const nlohmann::json &release);
};
