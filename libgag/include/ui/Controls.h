// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Containers.h"
#include <optional>

namespace GAGCore
{
class DrawableSurface;
class Sprite;
} // namespace GAGCore

namespace GAGGUI::ui
{
enum class TextAlign
{
	Left,
	Center,
	Right
};

struct TextOptions
{
	FontRole role = FontRole::Body;
	bool muted = false;
	TextAlign align = TextAlign::Left;
	std::optional<GAGCore::Color> color;
};
// One line, ellipsized when it does not fit.
Element label(const std::string &text, TextOptions options = {});
Element heading(const std::string &text);
Element title(const std::string &text);
Element caption(const std::string &text, bool muted = true);
// Wrapping text whose height follows its width.
Element paragraph(const std::string &text, TextOptions options = {});

struct ButtonOptions
{
	bool primary = false;
	bool selected = false;
	bool enabled = true;
	// No fill until hovered/pressed; used for list-like rows.
	bool flat = false;
	bool alignLeft = false;
	bool danger = false;
	SDL_Keycode shortcut = SDLK_UNKNOWN;
	FontRole role = FontRole::Body;
	// Minimum height in points; -1 uses the theme control height.
	double minHeight = -1;
	std::string tooltip;
};
Element button(const std::string &key, const std::string &text, std::function<void()> action,
			   ButtonOptions options = {});

Element toggle(const std::string &key, const std::string &text, bool value,
			   std::function<void(bool)> change, bool enabled = true);

struct ChoiceOptions
{
	std::vector<bool> enabled;
	std::string help;
	// Shown instead of the selected option (icon-like compact controls).
	std::string compactLabel;
	bool controlEnabled = true;
};
// Current value with a disclosure; opens a popup list on activation.
Element choice(const std::string &key, const std::vector<std::string> &options, int selected,
			   std::function<void(int)> change, ChoiceOptions extra = {});
// A value chosen elsewhere (a modal); shows the value and an ellipsis.
Element chooser(const std::string &key, const std::string &value, std::function<void()> open,
				bool enabled = true);
Element segments(const std::string &key, const std::vector<std::string> &options, int selected,
				 std::function<void(int)> change, std::vector<bool> enabled = {});

struct StepperOptions
{
	int step = 1;
	bool enabled = true;
	// Text shown for the value; defaults to the number.
	std::string valueText;
};
Element stepper(const std::string &key, int value, int minimum, int maximum,
				std::function<void(int)> change, StepperOptions options = {});

struct SliderOptions
{
	bool enabled = true;
	std::string valueText;
	std::string caption;
};
Element slider(const std::string &key, int value, int minimum, int maximum,
			   std::function<void(int)> change, SliderOptions options = {});

struct TextFieldOptions
{
	bool password = false;
	std::size_t maxLength = 0;
	std::string placeholder;
	std::function<void(const std::string &)> submit;
	// Start editing as soon as the field appears.
	bool autoFocus = false;
	bool enabled = true;
	// Keep edits in a draft until Enter or focus loss; Escape discards them.
	bool commitOnSubmit = false;
};
// Single-line editor. `change` receives the draft on every edit.
Element textField(const std::string &key, const std::string &value,
				  std::function<void(const std::string &)> change, TextFieldOptions options = {});

struct TextEditorOptions
{
	bool readOnly = false;
	// Height in text lines when the container does not bound it.
	int lines = 8;
	bool autoFocus = false;
	bool scrollToEnd = false;
};
// Multi-line editor / log viewer.
Element textEditor(const std::string &key, const std::string &value,
				   std::function<void(const std::string &)> change, TextEditorOptions options = {});

struct ListOptions
{
	// Per-row checkbox state; empty for plain selection lists.
	std::vector<bool> checked;
	std::function<void(int, bool)> toggle;
	std::vector<bool> enabled;
	// Enter or double activation on the selected row.
	std::function<void(int)> activate;
	// Custom row painter over the default text; receives the row rectangle.
	std::function<void(Canvas &, Rect, int, bool)> paintRow;
	// Height in text lines when the container does not bound it.
	int visibleRows = 8;
	std::string emptyText;
};
Element listView(const std::string &key, const std::vector<std::string> &items, int selected,
				 std::function<void(int)> select, ListOptions options = {});

Element progress(int value, int range, const std::string &text = "");

struct ImageOptions
{
	// Scale to fit the offered box, keeping aspect.
	bool fit = false;
	std::optional<Size> size;
	std::optional<GAGCore::Color> tint;
};
Element image(GAGCore::DrawableSurface *surface, ImageOptions options = {});
Element sprite(GAGCore::Sprite *sprite, int frame, std::optional<Size> size = {});
Element swatch(GAGCore::Color color, int points = 24);

struct CanvasOptions
{
	std::function<void(Point local, Host &)> tap;
	std::function<void(PointerPhase, Point local, Host &)> pointer;
	std::function<void(Point local)> hover;
	// Mouse wheel over the canvas; receives the wheel direction (+1 up, -1 down).
	std::function<void(int direction, Point local)> wheel;
	// Keep the preferred aspect when width is constrained.
	bool keepAspect = false;
	bool focusable = false;
	std::string accessibleText;
};
// Custom painter; `preferred` is the natural size in logical pixels.
Element canvas(const std::string &key, Size preferred,
			   std::function<void(Canvas &, Rect, const Frame &)> paint, CanvasOptions options = {});

// Host hook: attach browser editing owners and start auto-focused editors.
void bindTextControls(Host &host, Node &root);
} // namespace GAGGUI::ui
