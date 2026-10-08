// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GeneratorDefinition.h"
#include "online/OnlineStorage.h"
#include <map>
#include <memory>
#include <string>

namespace MapGeneration::JavaScript
{
inline constexpr unsigned ApiVersion = 1;
struct Package : std::enable_shared_from_this<Package>
{
	std::string id, name, description, author, entry, hash;
	unsigned revision = 1;
	bool editorOnly = false, hasStartingColonies = true;
	std::vector<std::string> tags;
	std::vector<GeneratorControl> controls;
	std::map<std::string, std::string> modules;
	std::map<std::string, std::map<std::string, std::string>> translations;
	// Control text pointers stay valid while a definition owns this package.
	std::vector<std::shared_ptr<const std::string>> labels;
	std::string canonical;
	static std::shared_ptr<const Package> parse(const std::string &bytes);
	static std::shared_ptr<const Package> load(const std::string &path);
	GeneratorDefinition definition(int handle) const;
};
struct LibraryOrigin
{
	std::string origin, libraryId, versionId, fileHash, packageHash;
};
class Library
{
	Online::OnlineStorage &storage;
	using PackageMap = std::map<std::string, std::shared_ptr<const Package>>;
	PackageMap packages;
	using Origins = std::map<std::string, LibraryOrigin>;
	Origins provenance;
	static std::string encode(const PackageMap &, const Origins &);
	static PackageMap decode(const std::string &, Origins &);
	void save(PackageMap candidate, Origins origins);

  public:
	explicit Library(Online::OnlineStorage &);
	const auto &entries() const { return packages; }
	std::string checkpoint() const { return encode(packages, provenance); }
	void rollback(const std::string &);
	void put(const std::string &bytes, const std::string &replace = {},
			 const LibraryOrigin *origin = nullptr);
	const auto &origins() const { return provenance; }
	void remove(const std::string &id);
	void publish() const;
};
// Invocations own their runtime and never retain a Game beyond the callback.
std::string invoke(const Package &, const char *callback, const GenerationRequest &, Game *game,
				   GenerationContext *, bool readonly = false);
void inspect(const Package &);
} // namespace MapGeneration::JavaScript
