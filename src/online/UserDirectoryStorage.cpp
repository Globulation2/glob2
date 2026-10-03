// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineStorage.h"

#include <ApplicationHost.h>
#include <FileManager.h>
#include <StreamBackend.h>
#include <Toolkit.h>

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
} // namespace

std::unique_ptr<OnlineStorage> makeUserDirectoryStorage()
{
	return std::make_unique<UserDirectoryStorage>(*GAGCore::Toolkit::getFileManager());
}
} // namespace Online
