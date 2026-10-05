// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scripting/javascript/ScriptRuntime.h"
#include "online/OnlineStorage.h"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Script
{
struct LibraryOrigin
{
	std::string origin, aiId, versionId, hash;
};
struct LibraryEntry
{
	std::string id, path;
	bool linked = false;
	Metadata metadata;
	std::optional<LibraryOrigin> online;
};
// Library state is local configuration, never a simulation dependency. Games
// receive frozen source bytes, not a library ID or a path.
class Library
{
	Online::OnlineStorage &storage;
	std::vector<LibraryEntry> entries_;
	unsigned next = 1, revision = 1;
	std::string encode() const;
	void decode(const std::string &bytes);

  public:
	explicit Library(Online::OnlineStorage &storage);
	const std::vector<LibraryEntry> &entries() const { return entries_; }
	const LibraryEntry &get(const std::string &id) const;
	std::string put(const std::string &source, const std::string &filename,
					const std::string &replace = {}, const std::string &externalPath = {},
					std::optional<LibraryOrigin> online = std::nullopt);
	void remove(const std::string &id);
	// Call only after the registry is durably persisted. Never touches linked paths.
	void collectUnusedSources();
	std::string source(const std::string &id) const;
	std::string configuration(const std::string &id) const;
	// The screen retains a checkpoint until browser persistence succeeds.
	std::string checkpoint() const { return encode(); }
	void rollback(const std::string &checkpoint);
};
} // namespace Script
