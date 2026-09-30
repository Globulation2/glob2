// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/FileListing.h"
#include "MapHeader.h"
#include <FileManager.h>
#include <TextSort.h>
#include <Toolkit.h>
#include <algorithm>
#include <map>

namespace Glob2UI
{
namespace
{
std::vector<std::string> scan(const std::string &directory, const std::string &extension,
							  const std::function<std::string(const std::string &)> &display)
{
	std::map<std::string, std::string> byName;
	auto &files = *GAGCore::Toolkit::getFileManager();
	if (files.initDirectoryListing(directory, extension, false))
	{
		std::string filename;
		while (!(filename = files.getNextDirectoryEntry()).empty())
		{
			if (files.isDir(directory + "/" + filename))
				continue;
			const auto name = display(filename);
			if (!name.empty())
				byName[name] = filename;
		}
	}
	std::vector<std::string> names;
	for (const auto &entry : byName)
		names.push_back(entry.first);
	std::sort(names.begin(), names.end(), GAGCore::naturalStringSort);
	return names;
}
} // namespace

std::vector<std::string> listFiles(const std::string &directory, const std::string &extension)
{
	return scan(directory, extension,
				[&](const std::string &filename)
				{
					const auto suffix = "." + extension;
					if (filename.size() > suffix.size() && filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0)
						return filename.substr(0, filename.size() - suffix.size());
					return extension.empty() ? filename : std::string();
				});
}

FileCatalog::FileCatalog(std::string directory, std::string extension)
	: dir(std::move(directory)), ext(std::move(extension))
{
	refresh();
}

void FileCatalog::refresh()
{
	auto display = [this](const std::string &filename) { return glob2FilenameToName(dir + "/" + filename); };
	entries = scan(dir, ext, display);
	if (!ext.empty())
	{
		auto compressed = scan(dir, ext + ".gz", display);
		for (const auto &name : compressed)
			if (std::find(entries.begin(), entries.end(), name) == entries.end())
				entries.push_back(name);
		std::sort(entries.begin(), entries.end(), GAGCore::naturalStringSort);
	}
}

std::string FileCatalog::path(const std::string &name) const
{
	const std::string raw = glob2NameToFilename(dir, name, ext);
	return glob2PreferGzipReadPath(*GAGCore::Toolkit::getFileManager(), raw);
}

int FileCatalog::indexOf(const std::string &name) const
{
	const auto it = std::find(entries.begin(), entries.end(), name);
	return it == entries.end() ? -1 : int(it - entries.begin());
}
} // namespace Glob2UI
