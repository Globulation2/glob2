// SPDX-License-Identifier: GPL-3.0-or-later
#include "TeamLayout.h"

#include <FormatableString.h>
#include <StringTable.h>
#include <Toolkit.h>

#include <algorithm>
#include <map>

namespace TeamLayout
{
namespace
{
std::string text(const char *key)
{
	return GAGCore::Toolkit::getStringTable()->getString(key);
}
} // namespace

Layout classify(const std::vector<int> &alliances, const std::vector<bool> &ai)
{
	Layout layout;
	const int n = int(alliances.size());
	std::map<int, std::vector<int>> groups;
	for (int i = 0; i < n; ++i)
		groups[alliances[std::size_t(i)]].push_back(i);
	if (n == 0 || int(groups.size()) == n)
	{
		layout.kind = Layout::FreeForAll;
		return layout;
	}
	if (groups.size() == 2 && int(ai.size()) == n)
	{
		// Each side all people or all AIs, and one of each.
		bool pure = true;
		for (const auto &group : groups)
			for (int i : group.second)
				pure = pure && ai[std::size_t(i)] == ai[std::size_t(group.second.front())];
		if (pure && ai[std::size_t(groups.begin()->second.front())] != ai[std::size_t(groups.rbegin()->second.front())])
		{
			layout.kind = Layout::HumansVsAI;
			return layout;
		}
	}
	for (const auto &group : groups)
		layout.sizes.push_back(int(group.second.size()));
	std::sort(layout.sizes.begin(), layout.sizes.end());
	const bool even = groups.size() >= 3 && layout.sizes.front() == layout.sizes.back() && layout.sizes.front() >= 2;
	if (groups.size() == 2 || even)
	{
		layout.kind = Layout::Split;
		if (layout.oneVsAll())
			for (const auto &group : groups)
				if (group.second.size() == 1)
					layout.lone = group.second.front();
		return layout;
	}
	layout.sizes.clear();
	return layout;
}

std::vector<Layout> offered(int colonies, bool oneVsAll)
{
	std::vector<Layout> options{Layout{Layout::FreeForAll, {}, -1}};
	// Two colonies apart are already the free-for-all.
	for (int small = colonies / 2; small >= (oneVsAll ? 1 : 2) && colonies > 2; --small)
		options.push_back(Layout{Layout::Split, {small, colonies - small}, -1});
	for (int size = colonies / 3; size >= 2; --size)
		if (colonies % size == 0)
			options.push_back(Layout{Layout::Split, std::vector<int>(std::size_t(colonies / size), size), -1});
	return options;
}

std::vector<int> alliances(const Layout &layout, int colonies, int focus)
{
	std::vector<int> result(std::size_t(std::max(colonies, 0)));
	if (layout.kind == Layout::FreeForAll)
		for (int i = 0; i < colonies; ++i)
			result[std::size_t(i)] = i;
	else if (layout.kind == Layout::Split && layout.sizes.size() == 2)
	{
		// The smaller side is the focus colony and the ones after it.
		focus = std::clamp(focus, 0, std::max(colonies - 1, 0));
		for (int i = 0; i < colonies; ++i)
			result[std::size_t(i)] = (i - focus + colonies) % colonies < layout.sizes[0] ? 0 : 1;
		// Dense and in order of first appearance, as setupTeams() renumbers them.
		if (!result.empty() && result[0] == 1)
			for (int &alliance : result)
				alliance = 1 - alliance;
	}
	else if (layout.kind == Layout::Split && !layout.sizes.empty())
		for (int i = 0; i < colonies; ++i)
			result[std::size_t(i)] = i / layout.sizes[0];
	else
		result.clear();
	return result;
}

int indexOf(const std::vector<Layout> &options, const Layout &current, int focus)
{
	for (int i = 0; i < int(options.size()); ++i)
	{
		const Layout &option = options[std::size_t(i)];
		if (option.kind == current.kind && option.sizes == current.sizes && (!current.oneVsAll() || current.lone == focus))
			return i;
	}
	return -1;
}

const char *legacyFormat(const Layout &layout, bool loneIsYou)
{
	if (layout.kind == Layout::FreeForAll)
		return "FFA";
	if (layout.kind == Layout::Split && layout.sizes == std::vector<int>{2, 2})
		return "2 vs 2";
	if (layout.oneVsAll() && loneIsYou)
		return "You vs all";
	return "Custom teams";
}

std::string label(const Layout &layout, bool loneIsYou, const std::string &loneName)
{
	switch (layout.kind)
	{
	case Layout::FreeForAll:
		return text("[FFA]");
	case Layout::HumansVsAI:
		return text("[room humans vs ai]");
	case Layout::Custom:
		return text("[Custom teams]");
	case Layout::Split:
		break;
	}
	if (layout.oneVsAll())
		return loneIsYou ? text("[You vs all]") : GAGCore::FormattableString(text("[%0 vs all]")).arg(loneName);
	if (layout.sizes == std::vector<int>{2, 2})
		return text("[2 vs 2]");
	std::string result = std::to_string(layout.sizes.empty() ? 0 : layout.sizes.front());
	for (std::size_t i = 1; i < layout.sizes.size(); ++i)
		result = GAGCore::FormattableString(text("[%0 vs %1]")).arg(result).arg(layout.sizes[i]);
	return result;
}
} // namespace TeamLayout
