// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingType.h"
#include "BuildingArtwork.h"
#include <nlohmann/json.hpp>
namespace Online
{
class OnlineStorage;
}
// Installed release identities are pinned explicitly. Only new maps consult selection.
class BuildingLibrary
{
  public:
	struct Selection
	{
		BuildingsTypes catalog;
		std::shared_ptr<const BuildingArtwork> artwork;
	};
	explicit BuildingLibrary(Online::OnlineStorage &storage) : storage(storage) {}
	nlohmann::json entries() const;
	void install(const nlohmann::json &manifest, const std::string &artwork);
	void select(const std::string &namespaceKey, bool enabled);
	void remove(const std::string &namespaceKey);
	Selection compose(const BuildingsTypes &stock) const;

  private:
	Online::OnlineStorage &storage;
	void save(const nlohmann::json &entries);
};
