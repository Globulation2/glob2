// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>

// The shape of a match's teams, read from the alliances of its open colonies: what the
// Teams selector of the custom game and room screens shows and offers. Nothing stores a
// layout; it is always derived, so hand-made teams of a known shape are named too.
namespace TeamLayout
{
struct Layout
{
	enum Kind
	{
		FreeForAll,
		// Two teams of any sizes, or three or more equal teams of at least two.
		Split,
		// Rooms only: the people on one side, the AIs on the other.
		HumansVsAI,
		Custom
	};
	Kind kind = Custom;
	// Split: the team sizes, smallest first ({3, 5}, {2, 2, 2}).
	std::vector<int> sizes;
	// One against all ({1, n-1}): the position of the lone colony among those classified.
	int lone = -1;

	bool oneVsAll() const { return kind == Split && sizes.size() == 2 && sizes[0] == 1; }
	bool operator==(const Layout &) const = default;
};

// `alliances` holds one entry per open colony, in slot order. `ai`, when given (rooms),
// flags the AI seats so that people against AIs reads as HumansVsAI.
Layout classify(const std::vector<int> &alliances, const std::vector<bool> &ai = {});
// What the selector offers for `colonies` open colonies: FFA, every two-team split from the
// most even down to one against all (when `oneVsAll`), then the equal splits into three or
// more teams.
std::vector<Layout> offered(int colonies, bool oneVsAll = true);
// The alliance of each open colony for an offered layout. `focus` is the position of the
// colony the smaller side of a two-team split is built around (yours, or the first).
// Empty for HumansVsAI and Custom, which are not built from a colony count.
std::vector<int> alliances(const Layout &layout, int colonies, int focus);
// The index in `options` that `current` is, or -1. One against all matches only when the
// lone colony is `focus`, since that is the colony the offered entry would isolate.
int indexOf(const std::vector<Layout> &options, const Layout &current, int focus);
// The custom game preferences store one of "FFA", "2 vs 2", "You vs all" and "Custom
// teams"; builds before the layout was derived refuse a saved draft with anything else.
const char *legacyFormat(const Layout &layout, bool loneIsYou);
// Translated: "FFA", "3 vs 5", "2 vs 2 vs 2", "You vs all", "Red vs all", "Custom teams".
// `loneName` names the lone colony of one against all when it is not yours.
std::string label(const Layout &layout, bool loneIsYou, const std::string &loneName);
} // namespace TeamLayout
