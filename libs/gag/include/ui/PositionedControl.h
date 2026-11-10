// SPDX-License-Identifier: GPL-3.0-or-later
// The value type a domain renderer describes its painted controls with. Kept apart
// from PositionedControls.h, which builds UI elements from it, so that headers holding
// these descriptions do not pull in the element tree, canvas and graphics context.
#pragma once
#include "Geometry.h"
#include "Inspection.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace GAGGUI::ui
{
// Layout models for controls painted by a domain renderer. Build these before
// painting or input; use these same bounds/callbacks in the domain hit-test.
// Coordinates are in the host's logical surface, not relative to root bounds.
struct PositionedControl
{
	std::string key, role = "button", label, identity;
	Rect bounds;
	std::optional<Rect> clip;
	ControlValue value;
	bool enabled = true, selected = false;
	std::vector<ActionKind> actions = {ActionKind::Activate};
	std::function<bool(const SemanticAction &)> perform;
	std::function<void(Point)> tap;
	bool contains(Point point) const
	{
		return bounds.contains(point) && (!clip || clip->contains(point));
	}
};
} // namespace GAGGUI::ui
