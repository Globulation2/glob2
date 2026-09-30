// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include <cmath>

namespace GAGCore
{
class GraphicContext;
class DrawableSurface;
} // namespace GAGCore

namespace GAGGUI::ui
{
enum class SizeClass
{
	Compact,
	Medium,
	Expanded
};

// One per-frame snapshot of everything layout may adapt to. Screens never
// query the host, the window or the settings themselves.
struct Presentation
{
	// Whole logical surface.
	Rect viewport;
	// Surface minus platform gutters (system bars, cutouts).
	Rect safe;
	// `safe` further reduced by the onscreen keyboard; dialogs and forms use it.
	Rect dialog;
	// Logical pixels per host point; theme metrics are specified in points.
	double unit = 1;
	// User text enlargement (1 = unchanged).
	double textScale = 1;
	bool touch = false;
	bool hover = true;

	int pt(double points) const { return int(std::lround(points * unit)); }
	double points(int pixels) const { return unit > 0 ? pixels / unit : pixels; }

	SizeClass widthClass() const { return classify(points(safe.w), 600, 960); }
	SizeClass heightClass() const { return classify(points(safe.h), 480, 720); }
	bool compact() const { return widthClass() == SizeClass::Compact; }
	bool expanded() const { return widthClass() == SizeClass::Expanded; }
	bool landscape() const { return safe.w > safe.h; }
	// Short landscape phones need every fixed control to fit beside content.
	bool shortLandscape() const
	{
		return landscape() && heightClass() == SizeClass::Compact;
	}
	// A phone: the smaller dimension is compact regardless of orientation.
	bool phone() const
	{
		return touch && std::min(points(viewport.w), points(viewport.h)) < 600;
	}

	static SizeClass classify(double value, double medium, double expanded)
	{
		return value < medium ? SizeClass::Compact
			   : value < expanded ? SizeClass::Medium
								  : SizeClass::Expanded;
	}
	static Presentation forSurface(int width, int height, double unit = 1, bool touch = false)
	{
		Presentation p;
		p.viewport = p.safe = p.dialog = {0, 0, width, height};
		p.unit = unit;
		p.touch = touch;
		p.hover = !touch;
		return p;
	}
	bool operator==(const Presentation &) const = default;
};

// Resolve from the live graphic context: safe insets, keyboard occlusion,
// density and input capabilities (see HostViewport.h / InterfacePresentation.h).
Presentation resolvePresentation(GAGCore::GraphicContext &context, double textScale = 1);
// Offscreen or non-window surfaces have no platform metrics.
Presentation resolvePresentation(GAGCore::DrawableSurface &surface, double textScale = 1);
} // namespace GAGGUI::ui
