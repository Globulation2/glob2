// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>
#include <InterfacePresentation.h>

// In-match surfaces deliberately do not share the frontend's paper theme.
// Measurements are window points; conversion to drawable units belongs to views.
namespace InGameTouchTheme
{
// HUD colours follow the player's in-game theme (Theme::hud).
const GAGCore::Color &ink();
const GAGCore::Color &paper();
const GAGCore::Color &field();
const GAGCore::Color &selected();
const GAGCore::Color &border();
const GAGCore::Color &readout();
const GAGCore::Color &dialTrack();
const GAGCore::Color &dialFill();
const GAGCore::Color &dialPadFill();
const GAGCore::Color &destroy();
const GAGCore::Color &erasePreview();
// Pending zone-brush cells, indexed by GameGUIToolManager::ZoneType (forbidden,
// guard, clearing, farm): tinted like the zone they add, dark when erasing.
inline const GAGCore::Color zonePreview[4] = {{235, 80, 70, 110}, {80, 130, 255, 120}, {245, 225, 90, 110}, {110, 240, 120, 110}};
inline const GAGCore::Color zonePreviewEdge[4] = {{255, 150, 140}, {160, 190, 255}, {255, 240, 150}, {190, 255, 190}};
inline constexpr double textScale = 1.0;
inline constexpr double target = 48;
// Reach of a flag's forgiving hit zone, for selecting and for touch grabs: the
// radius around its tile centre, independent of zoom. 30 points makes a circle
// of about 9.5 mm, the size one-handed thumb taps stop improving at (Parhi,
// Karlson and Bederson, 2006). Thumbs are least accurate near the screen's edges
// and corners (Hoober), so within the band the reach grows linearly to 36.
inline constexpr double flagReach = 30;
inline constexpr double flagReachEdge = 36;
inline constexpr double flagReachEdgeBand = 96;
inline constexpr double inspectorRow = 44;
inline constexpr double inspectorHeader = 40;
inline constexpr double ratioRow = 64;
inline constexpr double ratioLabel = 20;
inline constexpr double inspectorWide = 400;
inline constexpr double inspectorPortraitWidth = 360;
inline constexpr double inspectorLandscapeWidth = 480;
inline constexpr double paletteWidth = 248;
inline constexpr double tutorialLine = 24;
// The player's text-size preference. HUD text is drawn at
// gfx->textUnitsPerPoint(); rows sized by their text grow by this factor.
inline double textGrowth() { return GAGCore::userTextScale > 0 ? GAGCore::userTextScale : 1; }
inline double tutorialPitch() { return tutorialLine * textGrowth(); }
inline constexpr double paletteCell = 56;
inline constexpr double gap = 4;
// The phone palette rail: columns in portrait (buildings, flags) and landscape,
// and its distance from the thumb-side edge, which keeps drags out of the
// system back-gesture strip.
inline constexpr int railColumnsPortrait = 2;
inline constexpr int railColumnsLandscape = 4;
inline constexpr int railMaximumRows = 6;
inline constexpr double railInset = 12;
// The compact inspector's thumb dial: quarter rings centred on the thumb corner
// (outer radius shrinks to fit), swept from along the toolbar (start) to nearly
// straight up (end), stopping short of the screen edge.
inline constexpr double dialRadius = 286;
inline constexpr double dialMinimumRadius = 150;
inline constexpr double dialRingThickness = 28;
inline constexpr double dialRingGap = 8;
inline constexpr double dialSweepStart = 4;
inline constexpr double dialSweepEnd = 78;
inline constexpr double dialPad = 44; // Arc length of the −/+ pads at a slider's ends,
inline constexpr double dialPadMaximumAngle = 18; // capped so short inner arcs keep a slider.
inline constexpr double dialChipWidth = 104;
inline constexpr double dialChipHeight = 44;
// The brush rail (zones and editor): cell width, the smallest cell before it
// folds into two columns, the magnified size preview and the Undo chip. A
// stroke can be undone for this long after it lands.
inline constexpr double brushRailWidth = 48;
inline constexpr double brushRailMinimumCell = 32;
inline constexpr double brushPreviewSize = 96;
inline constexpr double brushUndoWidth = 80;
inline constexpr unsigned brushUndoMs = 6000;
// Lens strip cells, the map peek's largest side and internal minimap size, and
// how long a still press on the minimap takes to open the peek.
inline constexpr double lensWidth = 128;
inline constexpr double lensHeight = 48;
inline constexpr double peekSide = 300;
inline constexpr int peekMinimapSize = 256;
inline constexpr unsigned peekPressMs = 400;
inline constexpr double peekZoomStep = 1.25;
inline constexpr double peekButtonColumn = 96; // Landscape: buttons beside the map.
inline constexpr double dragThreshold = 8;
// Map release momentum needs more intent than the pan/tap threshold. Screen
// points keep this independent of both map zoom and display density.
inline constexpr double mapFlingTravelPoints = 16;
inline constexpr double fingerLift = 48;
inline constexpr double edgePanMargin = 24;
inline constexpr double edgePanPixelsPerSecond = 240;
// A completed map tap arms one-finger zoom for a second contact this soon
// and this close; the second contact then drags to zoom or taps to zoom in.
inline constexpr unsigned doubleTapWindowMs = 300;
inline constexpr double doubleTapRadius = 24;
inline constexpr double doubleTapZoomFactor = 2;
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
		font->pushStyle(GAGCore::Font::Style(GAGCore::Font::STYLE_NORMAL, ink()));
	}
	~TextStyle() { font->popStyle(); }
	TextStyle(const TextStyle &) = delete;
	TextStyle &operator=(const TextStyle &) = delete;
};
} // namespace InGameTouchTheme
