// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>

// Directory listings for list views, replacing the legacy FileList widgets.
namespace Glob2UI
{
// Display names (without the extension) of files in a virtual directory, naturally sorted.
std::vector<std::string> listFiles(const std::string &directory, const std::string &extension);

// Maps, saves and replays: display names through the glob2 naming rules, with
// compressed ".gz" copies listed once under the same name.
class FileCatalog
{
  public:
	FileCatalog(std::string directory, std::string extension);
	void refresh();
	const std::vector<std::string> &names() const { return entries; }
	// Full virtual path for a display name, preferring an existing ".gz" copy.
	std::string path(const std::string &name) const;
	int indexOf(const std::string &name) const;
	const std::string &directory() const { return dir; }
	const std::string &extension() const { return ext; }

  private:
	std::string dir, ext;
	std::vector<std::string> entries;
};
} // namespace Glob2UI
