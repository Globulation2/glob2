// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Element.h"
#include <initializer_list>

namespace GAGGUI::ui
{
enum class CrossAlign
{
	Stretch,
	Start,
	Center,
	End
};
enum class MainAlign
{
	Start,
	Center,
	End,
	SpaceBetween
};
enum class Alignment
{
	TopLeft,
	Top,
	TopRight,
	Left,
	Center,
	Right,
	BottomLeft,
	Bottom,
	BottomRight
};

struct StackOptions
{
	// Pixels between children; -1 uses the theme gap.
	int gap = -1;
	CrossAlign cross = CrossAlign::Stretch;
	MainAlign main = MainAlign::Start;
};

// Null children are skipped so builders can write conditional entries inline.
Element column(std::vector<Element> children, StackOptions options = {});
Element row(std::vector<Element> children, StackOptions options = {});
// Children share the same rectangle; later ones paint on top.
Element stack(std::vector<Element> children);
Element padding(Insets insets, Element child);
Element pad(int amount, Element child);
// Theme padding on every side.
Element padded(Element child);
Element align(Alignment alignment, Element child);
Element center(Element child);
Element sized(Size size, Element child);
Element width(int width, Element child);
Element height(int height, Element child);
Element constrained(Constraints constraints, Element child);
Element maxWidth(int width, Element child);
// Give a child a flex share of the remaining main-axis space.
Element expanded(Element child, int flex = 1);
Element spacer(int size = 0);
Element expandedSpacer(int flex = 1);
Element divider();

struct ScrollOptions
{
	// Keep the scrollbar visible even when the content fits.
	bool alwaysShowBar = false;
	// Shrink to content when it fits; otherwise fill the available height.
	bool shrinkToContent = true;
};
Element scroll(const std::string &key, Element child, ScrollOptions options = {});

struct WrapOptions
{
	int gap = -1;
	// Children narrower than this never share a row; -1 means theme dialog width / 3.
	int minChildWidth = -1;
	int maxColumns = 0;
	// Stretch every child to its column width.
	bool stretch = true;
};
// Equal-width grid whose column count follows the available width.
Element wrap(std::vector<Element> children, WrapOptions options = {});

// Chooses a subtree from the space it is offered, so one build works for every width.
Element adaptive(std::function<Element(const LayoutContext &, Size available)> choose);

struct CardOptions
{
	std::optional<GAGCore::Color> color;
	int radius = -1;
	int padding = -1;
	bool shadow = true;
	std::optional<GAGCore::Color> border;
};
Element card(Element child, CardOptions options = {});

// Body above a fixed action row. When the actions need more than half the
// height they join the body so nothing is stranded or obscured.
Element footer(Element body, Element actions);

struct FieldOptions
{
	std::string help;
	// Width of the control column in points when laid out side by side.
	double controlWidth = 220;
	// Force stacked or inline instead of adapting to width.
	std::optional<bool> stacked;
};
// A labelled control that stacks on narrow layouts and sits inline otherwise.
Element field(const std::string &label, Element control, FieldOptions options = {});
Element form(std::vector<Element> fields);
} // namespace GAGGUI::ui
