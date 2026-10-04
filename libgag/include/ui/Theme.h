// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include <functional>
#include "Presentation.h"
#include <GraphicContext.h>
#include <array>
#include <string>

namespace GAGGUI::ui
{
enum class FontRole
{
	Title,
	Heading,
	Body,
	Support,
	Caption
};
constexpr int fontRoleCount = 5;

struct Palette
{
	GAGCore::Color ink{36, 69, 49};
	GAGCore::Color muted{89, 108, 86};
	GAGCore::Color paper{240, 241, 223};
	GAGCore::Color panel{230, 231, 210};
	GAGCore::Color field{249, 250, 240};
	GAGCore::Color rail{225, 231, 209};
	GAGCore::Color line{194, 207, 183};
	GAGCore::Color accent{227, 192, 119};
	GAGCore::Color accentInk{17, 35, 32};
	GAGCore::Color selected{228, 199, 121};
	GAGCore::Color hover{169, 196, 157};
	GAGCore::Color focus{180, 110, 20};
	GAGCore::Color scrim{20, 32, 22, 160};
	GAGCore::Color disabled{222, 226, 212};
	GAGCore::Color danger{170, 60, 50};
	// Status text colours. On paper both keep at least 4.5:1 (WCAG AA for body text).
	GAGCore::Color success{40, 110, 50};
	// Something the player waits for or must do ("Waiting for Ana to be ready").
	GAGCore::Color warning{140, 80, 10};
	// Drop shadow under cards, panels and popups (alpha included).
	GAGCore::Color shadow{15, 39, 25, 35};
	// Tint over a pressed control.
	GAGCore::Color pressed{92, 130, 71, 55};
	// Behind everything when no artwork covers the window.
	GAGCore::Color backdrop{17, 35, 32};
	// Data placeholders: an unassigned swatch, an image still loading.
	GAGCore::Color neutral{160, 172, 149};
	GAGCore::Color placeholder{211, 223, 197};
};

// Colours of the in-match HUD drawn outside the framework controls (touch
// toolbars, inspector, dial, lenses, readouts). Defaults are the dark theme's.
struct HudPalette
{
	GAGCore::Color ink{249, 232, 187};
	GAGCore::Color paper{43, 28, 66, 218};
	GAGCore::Color field{65, 43, 88, 235};
	GAGCore::Color selected{114, 78, 111, 245};
	GAGCore::Color border{199, 165, 87};
	GAGCore::Color readout{29, 18, 46, 240};
	GAGCore::Color dialTrack{29, 20, 43, 225};
	GAGCore::Color dialFill{199, 165, 87, 190};
	GAGCore::Color dialPadFill{82, 56, 108, 245};
	GAGCore::Color destroy{81, 36, 60, 235};
	GAGCore::Color erasePreview{20, 14, 30, 140};
};

// What a menu screen draws behind its panels.
struct Backdrop
{
	enum class Kind
	{
		// The live colony, or its still image while it loads.
		Colony,
		// One image, cropped to cover the window.
		Image,
		// The classic tiled grass of the original menus.
		Terrain,
		// Only the backdrop colour.
		Solid
	};
	Kind kind = Kind::Colony;
	// Image path for Kind::Image.
	std::string image;
	// Wash over the artwork (alpha included); fully transparent for none.
	GAGCore::Color veil{240, 241, 223, 42};
	// Optional wordmark image shown as is; empty recolours the shipped wordmark
	// with the palette's ink and accent.
	std::string wordmark;
};

class Canvas;
struct ButtonPaintState
{
	bool primary = false, selected = false, enabled = true, hovered = false, pressed = false;
	// Borderless buttons (list rows, tabs, toolbar items) usually keep the palette look.
	bool flat = false;
};

// Metrics are host points; `Metrics` is their per-frame pixel resolution.
struct Theme
{
	// Stable identifier (file name and preference value) and display name.
	std::string id = "light", name = "Light";
	Palette palette;
	HudPalette hud;
	Backdrop backdrop;
	// Toolkit font names by role for pointer and touch presentations.
	std::array<std::string, fontRoleCount> fonts{"menu", "menu", "standard", "little", "little"};
	std::array<std::string, fontRoleCount> touchFonts{"menu", "menu", "frontend-body",
													 "frontend-support", "frontend-support"};
	double controlHeight = 34, touchControlHeight = 48;
	double gap = 8, padding = 16, radius = 6, minTouchTarget = 48;
	double dialogMaxWidth = 640, panelMaxWidth = 960;
	double scrollbarWidth = 6, touchScrollbarWidth = 10;
	double focusRing = 2, lineGap = 4, stepperSide = 32, touchStepperSide = 48;
	// Text enlargement on touch hosts before the player's preference; touch
	// text is then sized in points (see Presentation::textUnit).
	double touchTextScale = 1;
	// Optional classic painter for button backgrounds; return false to use the
	// palette. Lets a theme reuse legacy sprite artwork for an unchanged look.
	std::function<bool(Canvas &, Rect, const ButtonPaintState &)> buttonPainter;

	const std::string &fontName(FontRole role, bool touch) const
	{
		return (touch ? touchFonts : fonts)[int(role)];
	}
};

struct Metrics
{
	int control = 34, gap = 8, halfGap = 4, padding = 16, radius = 6, minTarget = 32;
	int dialogMaxWidth = 640, panelMaxWidth = 960, scrollbar = 6, focusRing = 2, lineGap = 4;
	int stepperSide = 32;
	bool touch = false;
};

inline Metrics resolveMetrics(const Theme &theme, const Presentation &p)
{
	Metrics m;
	m.touch = p.touch;
	m.control = p.pt(p.touch ? theme.touchControlHeight : theme.controlHeight);
	m.gap = p.pt(theme.gap);
	m.halfGap = std::max(1, m.gap / 2);
	m.padding = p.pt(theme.padding);
	m.radius = p.pt(theme.radius);
	m.minTarget = p.pt(p.touch ? theme.minTouchTarget : theme.controlHeight);
	m.dialogMaxWidth = p.pt(theme.dialogMaxWidth);
	m.panelMaxWidth = p.pt(theme.panelMaxWidth);
	m.scrollbar = p.pt(p.touch ? theme.touchScrollbarWidth : theme.scrollbarWidth);
	m.focusRing = std::max(1, p.pt(theme.focusRing));
	m.lineGap = p.pt(theme.lineGap);
	m.stepperSide = p.pt(p.touch ? theme.touchStepperSide : theme.stepperSide);
	return m;
}
} // namespace GAGGUI::ui
