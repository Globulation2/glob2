// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MAP_RENDER_H
#define MAP_RENDER_H

#include <string>
#include <vector>

class Game;

/// Whole-map PNGs through the game's own renderer, with fog off so every team
/// is visible at once, and with an optional scalar field painted over the top.
///
/// A map preview is terrain; this is the picture a player would see if they
/// could see everything, which is what questions about placement need. It is
/// read-only with respect to the simulation: it draws the state it is given and
/// changes nothing, so a run that renders executes identically to one that does
/// not (verified by comparing per-tick checksums).
namespace MapRender
{
/// A scalar value per tile. Alpha is each value's share of the field's own
/// maximum, so a field is readable without the renderer knowing its units.
struct Field
{
	int width = 0, height = 0;
	std::vector<int> values;
	int red = 0, green = 192, blue = 255;
};

/// Bring up an offscreen graphic context and the drawing assets, once. Safe to
/// call from a headless run, where none of it was loaded: the renderer needs
/// unit skins and per-building-type sprites that the headless path skips.
void ensureAssets();

/// Render `game` whole into `path`. The long edge is capped at `maximumPixels`
/// (0 for no cap); at 32 pixels a tile a 128-tile map is 4096 across.
/// Uses a temporary software surface without resizing or presenting a live window.
void toPng(Game &game, const std::string &path, int maximumPixels, const Field *field = nullptr);

/// Read a field file: "width height" then width*height integers.
Field readField(const std::string &path);
} // namespace MapRender

#endif
