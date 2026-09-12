// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

struct LobbyMapEntry
{
	std::string path, name;
};
inline std::vector<LobbyMapEntry> lobbyMapCatalog(const std::vector<std::filesystem::path> &roots)
{
	namespace fs = std::filesystem;
	std::set<fs::path> seen;
	std::vector<LobbyMapEntry> entries;
	for (const auto &root : roots)
	{
		std::error_code ec;
		for (fs::recursive_directory_iterator
				 it(root, fs::directory_options::skip_permission_denied, ec),
			 end;
			 it != end && !ec; it.increment(ec))
		{
			if (!it->is_regular_file(ec) || it->path().extension() != ".map")
				continue;
			auto canonical = fs::canonical(it->path(), ec);
			if (ec || !seen.insert(canonical).second)
				continue;
			auto name = canonical.stem().string();
			std::replace(name.begin(), name.end(), '_', ' ');
			entries.push_back({canonical.string(), name});
		}
	}
	std::map<std::string, int> names;
	for (auto &entry : entries)
		++names[entry.name];
	std::vector<std::string> labels;
	for (const auto &entry : entries)
	{
		std::string label = entry.name;
		if (names[entry.name] > 1)
		{
			fs::path parent = fs::path(entry.path).parent_path(), suffix = parent.filename();
			while (true)
			{
				int matches = 0;
				for (const auto &other : entries)
					if (other.name == entry.name)
					{
						auto otherParent = fs::path(other.path).parent_path().string(),
							 tail = suffix.string();
						if (otherParent.size() >= tail.size() &&
							otherParent.compare(otherParent.size() - tail.size(), tail.size(),
												tail) == 0)
							++matches;
					}
				if (matches == 1 || parent == parent.root_path())
					break;
				parent = parent.parent_path();
				suffix = parent.filename() / suffix;
			}
			label += "  (" + suffix.string() + ")";
		}
		labels.push_back(label);
	}
	for (size_t i = 0; i < entries.size(); ++i)
		entries[i].name = labels[i];
	auto folded = [](std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(),
					   [](unsigned char c) { return std::tolower(c); });
		return s;
	};
	std::sort(entries.begin(), entries.end(),
			  [&](const auto &a, const auto &b)
			  {
				  return std::make_pair(folded(a.name), a.path) <
						 std::make_pair(folded(b.name), b.path);
			  });
	return entries;
}
