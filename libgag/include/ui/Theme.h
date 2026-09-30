// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
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
};

// Metrics are host points; `Metrics` is their per-frame pixel resolution.
struct Theme
{
	Palette palette;
	// Toolkit font names by role for pointer and touch presentations.
	std::array<std::string, fontRoleCount> fonts{"menu", "menu", "standard", "little", "little"};
	std::array<std::string, fontRoleCount> touchFonts{"menu", "menu", "frontend-body",
													 "frontend-support", "frontend-support"};
	double controlHeight = 34, touchControlHeight = 48;
	double gap = 8, padding = 16, radius = 6, minTouchTarget = 48;
	double dialogMaxWidth = 640, panelMaxWidth = 960;
	double scrollbarWidth = 6, touchScrollbarWidth = 10;
	double focusRing = 2, lineGap = 4, stepperSide = 32, touchStepperSide = 48;

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
