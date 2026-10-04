// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapCache.h"
#include "MapCatalog.h"
#include "OnlineResources.h"
#include "QuickMatchScreen.h"
#include "ui/FrontendUI.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GAGGUI
{
class ScreenStack;
}
namespace Online
{
class PlatformScope;
}
namespace Glob2UI
{
class PreviewImages;
}
class MapPreview;

// The map catalog in the game (multiplayer mock-ups, screen group 7 B and C):
// Browse public maps (search, size, colonies, sort, detail with preview, Use
// in a room, Like, map page, Report) and My maps (upload, validation status,
// visibility, update, delete). Previews are the engine's PNGs from the
// server; "Use in a room" downloads the map into the content-addressed map
// cache and hands it to the room screen (Online::useMapInRoom).
class OnlineMapsScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "online_maps"; }
	enum
	{
		BACK = 1,
		// "Use in a room" chose a map: OPEN_ROOM asks the hub to open a room with it
		// (Online::pendingRoomMap); USED_IN_ROOM gave it to the open room.
		OPEN_ROOM = 2,
		USED_IN_ROOM = 3
	};
	enum class Tab
	{
		Browse,
		Mine
	};
	// Where the screen returns to, which names Back and decides what "Use in a
	// room" does once the map is downloaded.
	enum class Origin
	{
		Other, // keeps the map for the next room the player hosts
		Hub,   // closes with OPEN_ROOM
		Room   // the open room takes the map; closes with USED_IN_ROOM
	};
	explicit OnlineMapsScreen(GAGGUI::ScreenStack &screens, Tab tab = Tab::Browse);
	// Harnesses: fixed catalog and preview files, no network.
	struct Data
	{
		std::string instance;
		std::vector<Online::MapInfo> browse, mine;
		std::map<std::string, Online::MapDetail> details; // by map id
		std::map<std::string, std::string> previewFiles;  // preview URL -> local PNG
		bool signedIn = true, guest = false;
		std::int64_t now = 0;
	};
	OnlineMapsScreen(GAGGUI::ScreenStack &screens, Tab tab, Data data);
	~OnlineMapsScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void onTimer(Uint32 tick) override;

	// Semantic entry points.
	void selectTab(Tab tab);
	void select(int index);
	void openDetail(bool open); // phones: the selected map's sheet
	void setSearch(const std::string &text);
	void setSort(int index);
	void setSize(int index);
	void setColonies(int index);
	void clearFilters();
	void useInRoom();
	void toggleLike();
	void openMapPage();
	void beginReport();
	void sendReport(int reason);
	void upload();
	void update(const std::string &mapId);
	void setVisibility(const std::string &mapId, const std::string &visibility);
	void remove(const std::string &mapId);
	void loadMore();
	void setOrigin(Origin value) { origin = value; }

  protected:
	void onEscape() override;

  private:
	const std::vector<Online::MapInfo> &list() const;
	const Online::MapInfo *selectedMap() const;
	void reload();
	void fetchDetail(const std::string &mapId);
	GAGCore::DrawableSurface *previewOf(const Online::MapInfo &map);
	std::string facts(const Online::MapInfo &map, bool withVersion) const;
	Glob2UI::Element mapCard(int index, const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element detailPanel(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element browseBody(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element mineBody(const Glob2UI::Presentation &p, bool phone);
	Glob2UI::Element mineRow(const Online::MapInfo &map, const Glob2UI::Presentation &p, bool phone);
	void shareFile(const std::string &path, const std::string &mapId);

	GAGGUI::ScreenStack &screens;
	bool live;
	Data data;
	Tab tab;
	Origin origin = Origin::Other;
	std::string backLabel() const;
	Online::MapQuery query;
	int sizeChoice = 0, coloniesChoice = 0, sortChoice = 0;
	std::string cursor[2];
	bool loading = false;
	std::string problem, status;
	int selected = -1;
	bool detailOpen = false;
	bool reporting = false;
	std::unique_ptr<Glob2UI::PreviewImages> previews;
	std::unique_ptr<Online::MapCache::Download> download;
	std::optional<Online::MapInfo> downloading;
	std::map<std::string, bool> detailRequested;
	// This screen's platform calls, cancelled when it closes (made on first use:
	// fixture screens never touch the platform).
	std::unique_ptr<Online::PlatformScope> scope;
	Online::PlatformScope &calls();
	SearchStrip::Ticker searchTicker;
	bool started = false;
};

// The upload form (title, description, who can use it) and its progress:
// uploading, the server checking the map and drawing its preview, then
// ready or rejected with the reason. Opened from My maps, the map chooser and
// the editor's Share online action.
class MapShareScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "map_share"; }
	enum
	{
		CLOSED = 1,
		SHARED = 2
	};
	// path: a map file (FileManager path, possibly .gz). mapId: add a version
	// to that catalog map instead of creating one.
	MapShareScreen(std::string path, std::string mapId = {}, std::string title = {});
	~MapShareScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void onTimer(Uint32 tick) override;

	// Semantic entry points.
	void setTitle(const std::string &value);
	void setDescription(const std::string &value);
	void setVisibility(int index); // 0 public, 1 unlisted, 2 private
	void submit();
	void close();
	// Harnesses: show a stage of the flow with a server version.
	void presentProgress(Online::MapShare::Stage stage, const Online::MapVersionInfo &version);

  protected:
	void onEscape() override { close(); }

  private:
	std::string path, mapId;
	std::string title, description;
	int visibility = 1;
	std::string fileName;
	std::size_t fileSize = 0;
	int colonies = 0;
	std::string bytes;
	std::string readError;
	std::unique_ptr<MapPreview> preview;
	std::unique_ptr<Online::MapShare> share;
	std::optional<Online::MapShare::Stage> shownStage;
	std::optional<Online::MapVersionInfo> shownVersion;
	bool guest = false;
};
