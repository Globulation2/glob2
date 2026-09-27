// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>

// In-match surfaces deliberately do not share the frontend's paper theme.
// Measurements are window points; conversion to drawable units belongs to views.
namespace InGameTouchTheme
{
inline const GAGCore::Color ink{249, 232, 187};
inline const GAGCore::Color paper{43, 28, 66, 218};
inline const GAGCore::Color field{65, 43, 88, 235};
inline const GAGCore::Color selected{114, 78, 111, 245};
inline const GAGCore::Color border{199, 165, 87};
inline constexpr double textScale = 1.0;
inline constexpr double target = 48;
inline constexpr double inspectorRow = 48;
inline constexpr double paletteWidth = 248;
inline constexpr double tutorialLine = 24;
inline constexpr double paletteCell = 56;
inline constexpr double gap = 4;
inline constexpr double dragThreshold = 8;
inline constexpr double fingerLift = 48;
inline constexpr double edgePanMargin = 24;
inline constexpr double edgePanPixelsPerSecond = 240;
class TextStyle
{
	GAGCore::Font *font;

  public:
	explicit TextStyle(GAGCore::Font *value) : font(value)
	{
		font->pushStyle(GAGCore::Font::Style(GAGCore::Font::STYLE_NORMAL, ink));
	}
	~TextStyle() { font->popStyle(); }
	TextStyle(const TextStyle &) = delete;
	TextStyle &operator=(const TextStyle &) = delete;
};
} // namespace InGameTouchTheme
