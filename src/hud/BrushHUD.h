// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ViewportTransform.h>
#include <string>
#include <vector>

// The phone brush rail shared by zone painting and the map editor: brush sizes
// as detents on the thumb-side edge (smallest nearest the thumb), a Paint/Erase
// toggle at its foot, a Pan toggle at its head, and an Undo chip beside its
// foot. Layout, hit testing and drawing use the same rectangles (drawable units).
namespace BrushHUD
{
enum class Part
{
	None,
	Mode,
	Pan,
	Detent,
	Undo,
};
struct Hit
{
	Part part = Part::None;
	int index = -1;
};
struct Layout
{
	std::vector<GAGCore::ViewRect> detents; // Index = brush figure.
	GAGCore::ViewRect mode, pan, undo, rail;
	bool left = false;
	double unit = 1;
};
//! `area` bounds the rail: its thumb-side edge, bottom and top.
Layout layout(GAGCore::ViewRect area, bool left, double unit, bool withMode, bool withPan, bool withUndo);
//! Preserve minimum targets, widening beyond two columns when needed. Empty
//! means no supported thumb-rail grid fits; closing a palette exposes more map area.
Layout boundedLayout(GAGCore::ViewRect area, bool left, double unit, bool withMode, bool withPan, bool withUndo);
Hit hit(const Layout &layout, GAGCore::ViewPoint point);
//! Detent nearest the point's height, for scrubbing along the rail; -1 if none.
int detentAt(const Layout &layout, GAGCore::ViewPoint point);
//! Scrubbing a bounded rail cannot select a detent through another surface.
int detentAt(const Layout &layout, GAGCore::ViewPoint point, GAGCore::ViewRect clip);
struct State
{
	unsigned figure = 0;
	bool erase = false, pan = false;
	int touched = -1; // Detent under the finger: its size is shown magnified.
	std::string modeLabel, panLabel, undoLabel;
};
void draw(const Layout &layout, const State &state);
//! Bound the rail, labels and magnified preview to their owning canvas.
void draw(const Layout &layout, const State &state, GAGCore::ViewRect clip);
} // namespace BrushHUD
