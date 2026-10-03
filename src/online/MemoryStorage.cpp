// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineStorage.h"

namespace Online
{
bool MemoryStorage::read(const std::string &path, std::string &contents)
{
	auto found = files.find(path);
	if (found == files.end())
		return false;
	contents = found->second;
	return true;
}

bool MemoryStorage::write(const std::string &path, const std::string &contents)
{
	if (failWrites)
		return false;
	files[path] = contents;
	return true;
}

void MemoryStorage::remove(const std::string &path)
{
	files.erase(path);
}

std::vector<std::string> MemoryStorage::list(const std::string &directory)
{
	std::vector<std::string> names;
	const auto prefix = directory.empty() ? std::string() : directory + "/";
	for (const auto &[path, contents] : files)
		if (path.compare(0, prefix.size(), prefix) == 0 &&
			path.find('/', prefix.size()) == std::string::npos)
			names.push_back(path.substr(prefix.size()));
	return names;
}
} // namespace Online
