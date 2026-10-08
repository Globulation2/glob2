// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "ui/OnlineUI.h"
#include "PlatformClient.h"
#include "GeneratorPackage.h"
#include <nlohmann/json.hpp>
#include <ApplicationHost.h>
namespace GAGGUI
{
class ScreenStack;
}
class OnlineGeneratorsScreen : public Glob2UI::Screen
{
	GAGGUI::ScreenStack &screens;
	Online::PlatformScope calls;
	Glob2UI::PreviewImages previews;
	std::unique_ptr<Online::OnlineStorage> storage;
	MapGeneration::JavaScript::Library library;
	std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	nlohmann::json catalogue = nlohmann::json::array(), detail, settings;
	std::string query, tags, cursor, status, before, replacing, origin;
	int version = 0, sort = 0, filter = 0;
	bool loading = false, downloading = false, mine = false, compatibleOnly = false;
	void fetch(bool more = false);
	void select(const std::string &id);
	void selectRelease(int index);
	void install();
	void failInstallation(const std::string &message);
	void useInRoom();

  public:
	explicit OnlineGeneratorsScreen(GAGGUI::ScreenStack &screens);
	const char *recordingId() const override { return "online_generators"; }
	Glob2UI::Element build(const Glob2UI::Presentation &) override;
	void onTimer(Uint32) override;

  protected:
	void onEscape() override;
};
