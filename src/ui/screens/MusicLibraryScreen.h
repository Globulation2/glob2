// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "ui/OnlineUI.h"
#include "MusicLibrary.h"
#include "HttpFetch.h"
#include <ApplicationHost.h>
#include <nlohmann/json.hpp>
#include <map>
#include <deque>
namespace GAGGUI
{
class ScreenStack;
}
class MusicLibraryScreen : public Glob2UI::Screen
{
  public:
	explicit MusicLibraryScreen(GAGGUI::ScreenStack &screens);
	~MusicLibraryScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &) override;
	void onTimer(Uint32) override;
	const char *recordingId() const override { return "music_library"; }

  protected:
	bool panel() const override { return false; }
	void onEscape() override
	{
		if (!persistence && !persistenceFailed)
			endExecute(0);
	}

  private:
	GAGGUI::ScreenStack &screens;
	Music::Library library;
	std::unique_ptr<Music::Library> temporary;
	std::unique_ptr<Music::ImportJob> job;
	std::vector<Music::Installed> installed;
	std::string origin, search, tag, notice, next, catalogPath;
	int tab = 0, sort = 0, license = 0, ai = 0, duration = 0;
	bool filters = false;
	bool requested = false, append = false, previewOnly = false, pendingReload = false;
	nlohmann::json items = nlohmann::json::array(), active;
	std::map<std::string, nlohmann::json> selected;
	std::deque<nlohmann::json> queue;
	std::unique_ptr<HttpFetch::Fetch> fetch;
	enum class FetchKind
	{
		Catalog,
		Track,
		Action
	} kind = FetchKind::Catalog;
	std::array<std::vector<unsigned char>, 3> tracks;
	unsigned track = 0;
	std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	bool persistenceFailed = false;
	std::filesystem::path previewRoot;
	Glob2UI::PreviewImages covers;
	void reload(bool more = false);
	void startDownload(nlohmann::json release, bool preview);
	void downloadTrack();
	void action(const std::string &id, const std::string &suffix, HttpFetch::Method method,
				nlohmann::json body = {});
	void refreshInstalled();
	void selectInstalled(const Music::Installed &);
};
