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
inline const GAGCore::Color readout{29, 18, 46, 240};
// Pending zone-brush cells, indexed by GameGUIToolManager::ZoneType (forbidden,
// guard, clearing): tinted like the zone they add, dark when erasing.
inline const GAGCore::Color zonePreview[3] = {{235, 80, 70, 110}, {80, 130, 255, 120}, {245, 225, 90, 110}};
inline const GAGCore::Color zonePreviewEdge[3] = {{255, 150, 140}, {160, 190, 255}, {255, 240, 150}};
inline const GAGCore::Color erasePreview{20, 14, 30, 140};
inline constexpr double textScale = 1.0;
inline constexpr double target = 48;
inline constexpr double inspectorRow = 44;
inline constexpr double inspectorHeader = 40;
inline constexpr double ratioRow = 64;
inline constexpr double ratioLabel = 20;
inline constexpr double inspectorWide = 400;
inline constexpr double inspectorPortraitWidth = 360;
inline constexpr double inspectorLandscapeWidth = 480;
inline constexpr double paletteWidth = 248;
inline constexpr double tutorialLine = 24;
inline constexpr double paletteCell = 56;
inline constexpr double gap = 4;
// The phone palette rail: columns in portrait (buildings, flags) and landscape,
// and its distance from the thumb-side edge, which keeps drags out of the
// system back-gesture strip.
inline constexpr int railColumnsPortrait = 2;
inline constexpr int railColumnsLandscape = 4;
inline constexpr int railMaximumRows = 6;
inline constexpr double railInset = 12;
inline constexpr double dragThreshold = 8;
inline constexpr double fingerLift = 48;
inline constexpr double edgePanMargin = 24;
inline constexpr double edgePanPixelsPerSecond = 240;
// A completed map tap arms one-finger zoom for a second contact this soon
// and this close; the second contact then drags to zoom or taps to reset 1:1.
inline constexpr unsigned doubleTapWindowMs = 300;
inline constexpr double doubleTapRadius = 24;
// Readouts sit above the finger that is changing their value.
inline constexpr double readoutLift = 60;
inline constexpr double readoutHeight = 40;
inline constexpr double readoutTextScale = 1.3;
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
