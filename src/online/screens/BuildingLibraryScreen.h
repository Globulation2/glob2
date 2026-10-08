// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "PlatformClient.h"
#include "BuildingLibrary.h"
class BuildingLibraryScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "building_library"; }
	BuildingLibraryScreen();
	Glob2UI::Element build(const Glob2UI::Presentation &) override;
	void onTimer(Uint32) override;

  private:
	BuildingLibrary library;
	Online::PlatformScope calls;
	nlohmann::json families = nlohmann::json::array();
	std::string status, cursor;
	bool started = false, busy = false;
	void reload(bool more = false);
	void install(const nlohmann::json &family, const nlohmann::json &release);
};
