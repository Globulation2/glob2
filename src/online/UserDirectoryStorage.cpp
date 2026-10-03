// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineStorage.h"

#include <ApplicationHost.h>
#include <FileManager.h>
#include <StreamBackend.h>
#include <Toolkit.h>

#include <filesystem>
#include <fstream>
#include <system_error>

namespace Online
{
namespace
{
class UserDirectoryStorage final : public OnlineStorage
{
	GAGCore::FileManager &files;
	std::vector<std::unique_ptr<GAGCore::ApplicationHost::Persistence>> pending;

  public:
	explicit UserDirectoryStorage(GAGCore::FileManager &files) : files(files)
	{
		files.addWriteSubdir("online");
		files.addWriteSubdir("online/maps");
		files.addWriteSubdir("ais");
	}
	bool read(const std::string &path, std::string &contents) override
	{
		std::unique_ptr<GAGCore::StreamBackend> stream(files.openInputStreamBackend(path));
		if (!stream || !stream->isValid())
			return false;
		stream->seekFromEnd(0);
		const auto size = stream->getPosition();
		stream->seekFromStart(0);
		contents.assign(size, '\0');
		return size == 0 || stream->readExact(contents.data(), size);
	}
	bool write(const std::string &path, const std::string &contents) override
	{
		return files.writeFileAtomic(path, contents);
	}
	void remove(const std::string &path) override
	{
		files.remove(path);
	}
	std::vector<std::string> list(const std::string &directory) override
	{
		std::vector<std::string> names;
		if (files.initDirectoryListing(directory, "", false))
			for (auto name = files.getNextDirectoryEntry(); !name.empty();
				 name = files.getNextDirectoryEntry())
				names.push_back(name);
		return names;
	}
	void persist() override
	{
		// Finished operations are dropped; a pending one is safe to release,
		// but keeping it lets the host finish without new requests piling up.
		std::erase_if(pending,
					  [](const auto &operation)
					  {
						  return operation->state() !=
								 GAGCore::ApplicationHost::PersistenceState::Pending;
					  });
		if (auto operation = GAGCore::ApplicationHost::persistStorage())
			pending.push_back(std::move(operation));
	}
};

class DirectoryStorage final : public OnlineStorage
{
	std::filesystem::path root;

  public:
	explicit DirectoryStorage(std::string directory) : root(std::move(directory)) {}
	bool read(const std::string &path, std::string &contents) override
	{
		std::ifstream in(root / path, std::ios::binary);
		if (!in)
			return false;
		contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		return !in.bad();
	}
	bool write(const std::string &path, const std::string &contents) override
	{
		const auto target = root / path;
		std::error_code ec;
		std::filesystem::create_directories(target.parent_path(), ec);
		const auto temporary = target.string() + ".tmp";
		{
			std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
			out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
			if (!out)
				return false;
		}
		std::filesystem::rename(temporary, target, ec);
		return !ec;
	}
	void remove(const std::string &path) override
	{
		std::error_code ec;
		std::filesystem::remove(root / path, ec);
	}
	std::vector<std::string> list(const std::string &directory) override
	{
		std::vector<std::string> names;
		std::error_code ec;
		for (std::filesystem::directory_iterator i(root / directory, ec), end; !ec && i != end; i.increment(ec))
			if (i->is_regular_file())
				names.push_back(i->path().filename().string());
		return names;
	}
	std::string location(const std::string &path) override
	{
		return (root / path).string();
	}
};
} // namespace

std::unique_ptr<OnlineStorage> makeDirectoryStorage(const std::string &root)
{
	return std::make_unique<DirectoryStorage>(root);
}

std::unique_ptr<OnlineStorage> makeUserDirectoryStorage()
{
	return std::make_unique<UserDirectoryStorage>(*GAGCore::Toolkit::getFileManager());
}
} // namespace Online
