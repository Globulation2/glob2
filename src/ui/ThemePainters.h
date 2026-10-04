// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ui/Theme.h>
#include <string>

namespace Glob2UI
{
// Paints bordered buttons with the original game's three-slice sprites
// (<sprite>0..5.png: left, middle and right caps, each followed by its
// highlight), scaled to the button height. Images load on first paint; while
// they are missing the palette look is used.
decltype(GAGGUI::ui::Theme::buttonPainter) spriteButtonPainter(const std::string &sprite);
} // namespace Glob2UI
