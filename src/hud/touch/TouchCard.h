// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>
#include <ViewportTransform.h>
#include <memory>

// Anti-aliased rounded cards and buttons for the tablet HUD: one look for the
// navigation dock, panels, palette tiles and confirmation buttons. Shapes are
// rasterised once per size, tone and display density and drawn as textures,
// like the dial's sectors, so a frame costs one surface blit per control.
namespace TouchCard
{
enum class Tone
{
	Surface,  // Panels and docks: the theme's paper, nearly opaque.
	Raised,   // Ordinary buttons and tiles.
	Selected, // The chosen option: a gold rim over the raised fill.
	Primary,  // Confirmation: a gold fill read with dark ink.
	Danger,   // Cancel and destroy.
	Disabled  // Unavailable: flat and dim.
};
enum class Glyph
{
	None,
	Check,
	Cross,
	Trash,
	Plus,
	Minus
};
struct Style
{
	Tone tone = Tone::Raised;
	double radius = 12; // points
	bool shadow = true;
	Glyph glyph = Glyph::None;
	// Where the glyph sits: centred, or in a square at the left of the card.
	bool glyphLeading = false;
	// Phones: tighter shadows for small screens (radius is the caller's).
	bool compact = false;
};
//! Text colour that reads on a tone.
GAGCore::Color ink(Tone tone);
class Painter
{
	struct Cache;
	std::unique_ptr<Cache> cache;

  public:
	Painter();
	~Painter();
	//! `rect` in drawable units. The shadow extends below and around it.
	void draw(GAGCore::ViewRect rect, const Style &style);
	//! The label area beside a leading glyph (drawable units).
	static GAGCore::ViewRect labelArea(GAGCore::ViewRect rect, const Style &style);
};
} // namespace TouchCard
