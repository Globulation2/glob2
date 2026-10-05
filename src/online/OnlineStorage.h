// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

// Small files of the online client in the user directory: instance settings
// and credentials (online/instances.json) and the map cache (online/maps/).
// Paths are relative to the user directory and use '/'.
namespace Online
{
class OnlineStorage
{
  public:
	virtual ~OnlineStorage() = default;
    virtual std::size_t size(const std::string &path) { std::string bytes; return read(path,bytes)?bytes.size():0; }
	virtual bool read(const std::string &path, std::string &contents) = 0;
	// Replaces the file only once the complete contents are written.
	virtual bool write(const std::string &path, const std::string &contents) = 0;
	virtual void remove(const std::string &path) = 0;
	// Names (not paths) of the files directly in a directory.
	virtual std::vector<std::string> list(const std::string &directory) = 0;
	// Asks the host to make written files durable. In the browser this syncs
	// the IndexedDB-backed file system (ApplicationHost::persistStorage);
	// natively writes are already durable.
	virtual void persist() {}
	// The path under which GAGCore::FileManager (and so the engine's map loader)
	// opens a stored file: the user-directory-relative path itself, or an absolute
	// path for storage rooted elsewhere.
	virtual std::string location(const std::string &path)
	{
		return path;
	}
};

// The user directory through GAGCore::FileManager, as preferences and saves
// use it. Requires an initialised Toolkit.
std::unique_ptr<OnlineStorage> makeUserDirectoryStorage();
// Files under an absolute directory (created on the first write), for tools and
// tests that need a cache of their own outside the user directory.
std::unique_ptr<OnlineStorage> makeDirectoryStorage(const std::string &root);

// In-memory files, for tests and as a fallback when the user directory is
// unavailable.
class MemoryStorage : public OnlineStorage
{
  public:
	bool read(const std::string &path, std::string &contents) override;
	bool write(const std::string &path, const std::string &contents) override;
	void remove(const std::string &path) override;
	std::vector<std::string> list(const std::string &directory) override;
	void persist() override
	{
		++persisted;
	}
	std::map<std::string, std::string> files;
	int persisted = 0;
	bool failWrites = false;
};
} // namespace Online
