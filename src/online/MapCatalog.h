// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "OnlineResources.h"
#include "PlatformApi.h"
#include "PlatformClient.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

// Client side of the map catalog (/api/v1/maps, docs/multiplayer: map
// catalog): list queries, and the share flow that creates a map, uploads a
// version and follows the server's validation and preview rendering.
namespace Online
{
struct MapQuery
{
	std::string search;
	// recent | likes | plays | downloads
	std::string sort = "plays";
	bool mine = false;
	int teams = 0;				  // 0: any
	int minSide = 0, maxSide = 0; // 0: any
	std::string cursor;
	int limit = 24;
	// /api/v1/maps?...
	std::string path() const;
};


// Visibility of a new upload when the player does not choose (Q10).
inline constexpr const char *DEFAULT_MAP_VISIBILITY = "unlisted";

// Shares a map: POST /api/v1/maps (unless adding a version to mapId), then
// POST /api/v1/maps/{id}/versions with the uncompressed bytes, then polls the
// map until the engine agent has validated the version and rendered its
// preview. Poll update() from the UI thread.
class MapShare
{
  public:
	enum class Stage
	{
		Creating,
		Uploading,
		Checking, // the server is loading the map and drawing its preview
		Ready,	  // valid; the preview may have failed (version().preview)
		Rejected, // the server could not load it: version().reason
		Failed	  // a request failed: error()
	};
	struct Details
	{
		std::string title, description;
		std::string visibility = DEFAULT_MAP_VISIBILITY;
		std::string madeWith = "hand"; // hand | generator
	};
	// mapId empty: create a new catalog map with details.
	MapShare(PlatformClient &client, std::string mapId, Details details, std::string bytes,
			 std::string simVersionKey, std::function<std::int64_t()> now,
			 std::int64_t pollIntervalMs = 2000);
	void update();
	Stage stage() const { return current; }
	bool finished() const { return current == Stage::Ready || current == Stage::Rejected || current == Stage::Failed; }
	const std::string &mapId() const { return id; }
	const std::optional<MapVersionInfo> &version() const { return uploaded; }
	const ApiError &error() const { return problem; }

  private:
	void create();
	void upload();
	void poll();

	// Cancelled with the share (closing the share screen).
	PlatformScope calls;
	std::string id;
	Details details;
	std::string bytes;
	std::string simKey;
	std::function<std::int64_t()> now;
	std::int64_t interval;
	Stage current = Stage::Creating;
	std::optional<MapVersionInfo> uploaded;
	ApiError problem;
	bool waiting = false;
	std::int64_t nextPoll = 0;
};
} // namespace Online
