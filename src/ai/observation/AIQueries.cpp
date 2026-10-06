// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIWorldView.h"
#include <algorithm>
#include <stdexcept>

namespace AIEngine
{
namespace
{
template<class View> const View* findSlot(std::span<const View> values, Uint16 gid)
{
	const auto found = std::lower_bound(values.begin(), values.end(), gid,
		[](const View& value, Uint16 id) { return value.identity.gid < id; });
	return found != values.end() && found->identity.gid == gid ? &*found : nullptr;
}
}
const BuildingView* AIWorldView::buildingAtSlot(Uint16 gid) const { return findSlot(buildings, gid); }
const UnitView* AIWorldView::unitAtSlot(Uint16 gid) const { return findSlot(units, gid); }
const BuildingView* AIWorldView::building(BuildingRef identity) const
{
	const auto* value = buildingAtSlot(identity.gid);
	return value && value->identity == identity ? value : nullptr;
}
const UnitView* AIWorldView::unit(UnitRef identity) const
{
	const auto* value = unitAtSlot(identity.gid);
	return value && value->identity == identity ? value : nullptr;
}
TileView AIWorldView::tile(int x, int y) const
{
	return tiles.at(tileIndex(x, y));
}
int AIWorldView::distanceSquared(int x1, int y1, int x2, int y2) const
{
	int dx = normalizeX(x1) - normalizeX(x2), dy = normalizeY(y1) - normalizeY(y2);
	dx = std::min(std::abs(dx), width - std::abs(dx));
	dy = std::min(std::abs(dy), height - std::abs(dy));
	return dx * dx + dy * dy;
}
} // namespace AIEngine
