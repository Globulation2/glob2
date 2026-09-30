// SPDX-License-Identifier: GPL-3.0-or-later
#include <ui/Controls.h>
#include <ui/Host.h>
#include <BrowserTextInput.h>
#include <GraphicContext.h>
#include <algorithm>
#include <cmath>

namespace GAGGUI::ui
{
namespace
{
GAGCore::Color inkFor(const Frame &frame, bool enabled, bool muted)
{
	const auto &p = frame.layout.theme.palette;
	return !enabled ? p.muted : muted ? p.muted : p.ink;
}

int textTop(Rect r, int lineHeight) { return r.y + std::max(0, (r.h - lineHeight) / 2); }

void drawLines(Frame &frame, Rect r, FontRole role, const std::vector<std::string> &lines,
			   TextAlign align, GAGCore::Color color, int lineGap, bool verticalCenter = true)
{
	const auto &m = frame.canvas.measurer();
	const int lineHeight = m.lineHeight(role);
	const int total = int(lines.size()) * lineHeight + int(lines.size() - 1) * lineGap;
	int y = verticalCenter ? textTop(r, total) : r.y;
	for (const auto &line : lines)
	{
		const int w = m.width(role, line);
		int x = r.x;
		if (align == TextAlign::Center)
			x = r.x + std::max(0, (r.w - w) / 2);
		else if (align == TextAlign::Right)
			x = r.x + std::max(0, r.w - w);
		frame.canvas.text({x, y}, role, line, color);
		y += lineHeight + lineGap;
	}
}

class Label : public Node
{
  public:
	Label(std::string text, TextOptions options) : text(std::move(text)), options(options) {}
	const char *name() const override { return "label"; }
	std::string accessibleText() const override { return text; }
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const int w = ctx.text.width(options.role, text);
		return c.clamp({std::min(w, c.maxW), ctx.text.lineHeight(options.role)});
	}
	void paint(Frame &frame) override
	{
		const auto &m = frame.canvas.measurer();
		const auto shown = ellipsize(m, options.role, text, bounds.w);
		drawLines(frame, bounds, options.role, {shown}, options.align,
				  options.color.value_or(inkFor(frame, true, options.muted)), 0);
	}

  private:
	std::string text;
	TextOptions options;
};

class Paragraph : public Node
{
  public:
	Paragraph(std::string text, TextOptions options) : text(std::move(text)), options(options) {}
	const char *name() const override { return "paragraph"; }
	std::string accessibleText() const override { return text; }
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const int limit = c.boundedW() ? c.maxW : ctx.text.width(options.role, text);
		const auto block = layoutText(ctx.text, options.role, text, std::max(1, limit), ctx.metrics.lineGap);
		return c.clamp({c.boundedW() && options.align != TextAlign::Left ? c.maxW : block.width, block.height});
	}
	void paint(Frame &frame) override
	{
		const auto &m = frame.canvas.measurer();
		const auto block = layoutText(m, options.role, text, std::max(1, bounds.w), frame.layout.metrics.lineGap);
		drawLines(frame, bounds, options.role, block.lines, options.align,
				  options.color.value_or(inkFor(frame, true, options.muted)),
				  frame.layout.metrics.lineGap, false);
	}

  private:
	std::string text;
	TextOptions options;
};

class Button : public Node
{
  public:
	Button(std::string key, std::string text, std::function<void()> action, ButtonOptions options)
		: text(std::move(text)), action(std::move(action)), options(options)
	{
		this->key = std::move(key);
	}
	const char *name() const override { return "button"; }
	std::string accessibleText() const override { return text; }
	bool interactive() const override { return true; }
	bool enabled() const override { return options.enabled; }
	SDL_Keycode shortcut() const override { return options.shortcut; }
	void tap(Point, Host &host) override
	{
		if (!options.enabled)
			return;
		auto callback = action;
		host.invalidate();
		if (callback)
			callback();
	}
	// Never below the theme control height, so touch targets stay reachable.
	int minHeight(const LayoutContext &ctx) const
	{
		return options.minHeight < 0 ? ctx.metrics.control
									 : std::max(ctx.metrics.control, ctx.presentation.pt(options.minHeight));
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const int pad = ctx.metrics.padding;
		const int natural = ctx.text.width(options.role, text) + 2 * pad;
		const int width = c.boundedW() ? std::min(natural, c.maxW) : natural;
		const auto block = layoutText(ctx.text, options.role, text, std::max(1, width - 2 * ctx.metrics.halfGap), ctx.metrics.lineGap);
		const int height = std::max(minHeight(ctx), block.height + 2 * ctx.metrics.halfGap);
		return c.clamp({width, height});
	}
	void paint(Frame &frame) override
	{
		const auto &p = frame.layout.theme.palette;
		const int radius = frame.layout.metrics.radius;
		const bool hovered = options.enabled && frame.hovered(bounds);
		const bool pressed = frame.pressed(key);
		GAGCore::Color fill = options.primary ? p.accent : options.selected ? p.selected : p.field;
		if (!options.enabled)
			fill = p.disabled;
		if (options.danger && options.enabled)
			fill = p.field;
		const auto &painter = frame.layout.theme.buttonPainter;
		const bool classic = painter && painter(frame.canvas, bounds, {options.primary, options.selected, options.enabled, hovered, pressed});
		if (!classic)
		{
			if (!options.flat || options.selected || options.primary || hovered || pressed)
				frame.canvas.fillRounded(bounds, radius, fill);
			if (hovered)
				frame.canvas.fillRounded(bounds, radius, p.hover.applyAlpha(70));
			if (pressed)
				frame.canvas.fillRounded(bounds, radius, GAGCore::Color(92, 130, 71, 55));
			if (!options.flat)
				frame.canvas.strokeRect(bounds, options.primary ? p.accentInk.applyAlpha(60) : p.line);
		}
		const auto &m = frame.canvas.measurer();
		const int inset = frame.layout.metrics.halfGap * 2;
		const Rect textRect = bounds.inset(Insets::symmetric(inset, frame.layout.metrics.halfGap));
		const auto block = layoutText(m, options.role, text, std::max(1, textRect.w), frame.layout.metrics.lineGap);
		GAGCore::Color ink = options.primary || classic ? p.accentInk : inkFor(frame, options.enabled, false);
		if (options.danger && options.enabled)
			ink = p.danger;
		drawLines(frame, textRect, options.role, block.lines,
				  options.alignLeft ? TextAlign::Left : TextAlign::Center, ink,
				  frame.layout.metrics.lineGap);
	}

  private:
	std::string text;
	std::function<void()> action;
	ButtonOptions options;
};

class Toggle : public Node
{
  public:
	Toggle(std::string key, std::string text, bool value, std::function<void(bool)> change, bool enabled)
		: text(std::move(text)), value(value), change(std::move(change)), enabledValue(enabled)
	{
		this->key = std::move(key);
	}
	const char *name() const override { return "toggle"; }
	std::string accessibleText() const override { return (value ? "[x] " : "[ ] ") + text; }
	bool interactive() const override { return true; }
	bool enabled() const override { return enabledValue; }
	void tap(Point, Host &host) override
	{
		auto callback = change;
		const bool next = !value;
		host.invalidate();
		if (callback)
			callback(next);
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const int box = ctx.metrics.control;
		const int width = c.boundedW() ? c.maxW : box + ctx.metrics.gap + ctx.text.width(FontRole::Body, text) + ctx.metrics.padding;
		const auto block = layoutText(ctx.text, FontRole::Body, text, std::max(1, width - box - ctx.metrics.gap), ctx.metrics.lineGap);
		return c.clamp({width, std::max(box, block.height + ctx.metrics.gap)});
	}
	void paint(Frame &frame) override
	{
		const auto &p = frame.layout.theme.palette;
		const auto &mt = frame.layout.metrics;
		const bool hovered = enabledValue && frame.hovered(bounds);
		if (hovered || frame.pressed(key))
			frame.canvas.fillRounded(bounds, mt.radius, p.hover.applyAlpha(60));
		const int side = std::min(mt.control - mt.gap, frame.layout.presentation.pt(22));
		const Rect mark{bounds.x + mt.halfGap, bounds.y + (bounds.h - side) / 2, side, side};
		frame.canvas.fillRounded(mark, std::max(2, mt.radius / 2), value ? p.accent : enabledValue ? p.field : p.disabled);
		frame.canvas.strokeRect(mark, enabledValue ? p.ink : p.muted);
		if (value)
			for (int t = 0; t < 2; ++t)
			{
				frame.canvas.line({mark.x + side / 5, mark.y + side / 2 + t}, {mark.x + side * 2 / 5, mark.y + side * 3 / 4 + t}, p.ink);
				frame.canvas.line({mark.x + side * 2 / 5, mark.y + side * 3 / 4 + t}, {mark.x + side * 4 / 5, mark.y + side / 4 + t}, p.ink);
			}
		const Rect textRect{mark.right() + mt.gap, bounds.y, std::max(1, bounds.right() - mark.right() - mt.gap), bounds.h};
		const auto block = layoutText(frame.canvas.measurer(), FontRole::Body, text, textRect.w, mt.lineGap);
		drawLines(frame, textRect, FontRole::Body, block.lines, TextAlign::Left, inkFor(frame, enabledValue, false), mt.lineGap);
	}

  private:
	std::string text;
	bool value;
	std::function<void(bool)> change;
	bool enabledValue;
};

// Shared look for value controls: field background with a trailing glyph.
class Disclosure : public Node
{
  public:
	Disclosure(std::string key, std::string label, bool enabled, std::string glyph)
		: label(std::move(label)), enabledValue(enabled), glyph(std::move(glyph))
	{
		this->key = std::move(key);
	}
	std::string accessibleText() const override { return label; }
	bool interactive() const override { return true; }
	bool enabled() const override { return enabledValue; }
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const int natural = ctx.text.width(FontRole::Body, label) + 3 * ctx.metrics.padding;
		const int width = c.boundedW() ? c.maxW : natural;
		const auto block = layoutText(ctx.text, FontRole::Body, label, std::max(1, width - 3 * ctx.metrics.padding + ctx.metrics.halfGap), ctx.metrics.lineGap);
		return c.clamp({width, std::max(ctx.metrics.control, block.height + ctx.metrics.gap)});
	}
	void paint(Frame &frame) override
	{
		const auto &p = frame.layout.theme.palette;
		const auto &mt = frame.layout.metrics;
		frame.canvas.fillRounded(bounds, mt.radius, enabledValue ? p.field : p.disabled);
		if (enabledValue && frame.hovered(bounds))
			frame.canvas.fillRounded(bounds, mt.radius, p.hover.applyAlpha(50));
		frame.canvas.strokeRect(bounds, p.line);
		const int slot = mt.padding + mt.gap;
		const Rect textRect{bounds.x + mt.gap, bounds.y, std::max(1, bounds.w - slot - mt.gap), bounds.h};
		const auto block = layoutText(frame.canvas.measurer(), FontRole::Body, label, textRect.w, mt.lineGap);
		drawLines(frame, textRect, FontRole::Body, block.lines, TextAlign::Left, inkFor(frame, enabledValue, false), mt.lineGap);
		const int gw = frame.canvas.measurer().width(FontRole::Support, glyph);
		frame.canvas.text({bounds.right() - mt.gap - gw, textTop(bounds, frame.canvas.measurer().lineHeight(FontRole::Support))}, FontRole::Support, glyph, p.muted);
	}

  protected:
	std::string label;
	bool enabledValue;
	std::string glyph;
};

class Choice : public Disclosure
{
  public:
	Choice(std::string key, std::vector<std::string> options, int selected, std::function<void(int)> change, ChoiceOptions extra)
		: Disclosure(key, extra.compactLabel.empty() ? (selected >= 0 && selected < int(options.size()) ? options[selected] : std::string()) : extra.compactLabel, extra.controlEnabled, "v"),
		  options(std::move(options)), selected(selected), change(std::move(change)), extra(std::move(extra))
	{
	}
	const char *name() const override { return "choice"; }
	void tap(Point, Host &host) override
	{
		PopupSpec spec;
		spec.anchor = bounds;
		spec.options = options;
		spec.enabled = extra.enabled;
		spec.selected = std::max(0, selected);
		spec.help = extra.help;
		auto callback = change;
		spec.pick = [callback, &host](int index)
		{
			host.invalidate();
			if (callback)
				callback(index);
		};
		host.openPopup(std::move(spec));
	}
	void activate(Host &host, int direction) override
	{
		if (direction == 0)
		{
			tap(bounds.center(), host);
			return;
		}
		int next = selected;
		for (int i = 0; i < int(options.size()); ++i)
		{
			next = std::clamp(next + direction, 0, int(options.size()) - 1);
			if (extra.enabled.empty() || extra.enabled[next])
				break;
		}
		if (next != selected && (extra.enabled.empty() || extra.enabled[next]))
		{
			auto callback = change;
			host.invalidate();
			if (callback)
				callback(next);
		}
	}

  private:
	std::vector<std::string> options;
	int selected;
	std::function<void(int)> change;
	ChoiceOptions extra;
};

class Chooser : public Disclosure
{
  public:
	Chooser(std::string key, std::string value, std::function<void()> open, bool enabled)
		: Disclosure(key, std::move(value), enabled, "..."), open(std::move(open))
	{
	}
	const char *name() const override { return "chooser"; }
	void tap(Point, Host &host) override
	{
		auto callback = open;
		host.invalidate();
		if (callback)
			callback();
	}

  private:
	std::function<void()> open;
};

class Stepper : public Node
{
  public:
	Stepper(std::string key, int value, int minimum, int maximum, std::function<void(int)> change, StepperOptions options)
		: value(value), minimum(minimum), maximum(maximum), change(std::move(change)), options(options)
	{
		this->key = std::move(key);
	}
	const char *name() const override { return "stepper"; }
	std::string accessibleText() const override { return valueText(); }
	std::string valueText() const { return options.valueText.empty() ? std::to_string(value) : options.valueText; }
	bool interactive() const override { return true; }
	bool enabled() const override { return options.enabled; }
	void step(Host &host, int direction)
	{
		const int next = std::clamp(value + direction * options.step, minimum, maximum);
		if (next == value)
			return;
		auto callback = change;
		host.invalidate();
		if (callback)
			callback(next);
	}
	void tap(Point point, Host &host) override
	{
		const int side = sideWidth;
		if (point.x < bounds.x + side)
			step(host, -1);
		else if (point.x >= bounds.right() - side)
			step(host, 1);
	}
	void activate(Host &host, int direction) override
	{
		if (direction != 0)
			step(host, direction);
	}
	bool keyDown(const KeyEvent &event, Host &host) override
	{
		if (event.sym == SDLK_LEFT || event.sym == SDLK_MINUS)
			step(host, -1);
		else if (event.sym == SDLK_RIGHT || event.sym == SDLK_PLUS || event.sym == SDLK_EQUALS)
			step(host, 1);
		else
			return false;
		return true;
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		sideWidth = ctx.metrics.stepperSide;
		const int natural = 2 * sideWidth + ctx.text.width(FontRole::Body, valueText()) + 2 * ctx.metrics.padding;
		return c.clamp({c.boundedW() ? std::min(std::max(natural, c.minW), c.maxW) : natural, ctx.metrics.control});
	}
	void paint(Frame &frame) override
	{
		const auto &p = frame.layout.theme.palette;
		const auto &mt = frame.layout.metrics;
		frame.canvas.fillRounded(bounds, mt.radius, options.enabled ? p.field : p.disabled);
		frame.canvas.strokeRect(bounds, p.line);
		const Rect left{bounds.x, bounds.y, sideWidth, bounds.h};
		const Rect right{bounds.right() - sideWidth, bounds.y, sideWidth, bounds.h};
		auto side = [&](Rect r, const char *glyph, bool ok)
		{
			if (ok && frame.hovered(r))
				frame.canvas.fillRounded(r, mt.radius, p.hover.applyAlpha(60));
			const auto &m = frame.canvas.measurer();
			frame.canvas.text({r.x + (r.w - m.width(FontRole::Body, glyph)) / 2, textTop(r, m.lineHeight(FontRole::Body))}, FontRole::Body, glyph, ok ? p.ink : p.muted);
		};
		side(left, "−", options.enabled && value > minimum);
		side(right, "+", options.enabled && value < maximum);
		frame.canvas.line({left.right(), bounds.y}, {left.right(), bounds.bottom()}, p.line);
		frame.canvas.line({right.x, bounds.y}, {right.x, bounds.bottom()}, p.line);
		const Rect middle{left.right(), bounds.y, std::max(1, right.x - left.right()), bounds.h};
		const auto shown = ellipsize(frame.canvas.measurer(), FontRole::Body, valueText(), middle.w - mt.gap);
		drawLines(frame, middle, FontRole::Body, {shown}, TextAlign::Center, inkFor(frame, options.enabled, false), 0);
	}

  private:
	int value, minimum, maximum, sideWidth = 32;
	std::function<void(int)> change;
	StepperOptions options;
};

class Slider : public Node
{
  public:
	Slider(std::string key, int value, int minimum, int maximum, std::function<void(int)> change, SliderOptions options)
		: value(value), minimum(minimum), maximum(std::max(minimum, maximum)), change(std::move(change)), options(options)
	{
		this->key = std::move(key);
	}
	const char *name() const override { return "slider"; }
	std::string accessibleText() const override { return options.valueText.empty() ? std::to_string(value) : options.valueText; }
	bool interactive() const override { return true; }
	bool enabled() const override { return options.enabled; }
	bool capturesPointer(Point) const override { return options.enabled; }
	Rect track() const
	{
		const int h = trackHeight;
		return {bounds.x + thumbWidth / 2, bounds.bottom() - h - thumbWidth / 2, std::max(1, bounds.w - thumbWidth), h};
	}
	void set(Host &host, int next)
	{
		next = std::clamp(next, minimum, maximum);
		if (next == value)
			return;
		value = next;
		auto callback = change;
		host.invalidate();
		if (callback)
			callback(next);
	}
	void pointer(PointerPhase phase, Point point, Host &host) override
	{
		if (phase == PointerPhase::Cancel)
			return;
		const Rect t = track();
		const double fraction = std::clamp(double(point.x - t.x) / std::max(1, t.w), 0.0, 1.0);
		set(host, minimum + int(std::lround(fraction * (maximum - minimum))));
	}
	void tap(Point point, Host &host) override { pointer(PointerPhase::Down, point, host); }
	void activate(Host &host, int direction) override
	{
		if (direction != 0)
			set(host, value + direction);
	}
	bool keyDown(const KeyEvent &event, Host &host) override
	{
		if (event.sym == SDLK_LEFT)
			set(host, value - 1);
		else if (event.sym == SDLK_RIGHT)
			set(host, value + 1);
		else if (event.sym == SDLK_HOME)
			set(host, minimum);
		else if (event.sym == SDLK_END)
			set(host, maximum);
		else
			return false;
		return true;
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		thumbWidth = ctx.presentation.pt(ctx.presentation.touch ? 18 : 12);
		trackHeight = std::max(2, ctx.presentation.pt(4));
		const int lineHeight = ctx.text.lineHeight(FontRole::Body);
		const bool captioned = !options.caption.empty() || !options.valueText.empty();
		const int height = std::max(ctx.metrics.control, (captioned ? lineHeight + ctx.metrics.halfGap : 0) + thumbWidth + ctx.metrics.gap);
		return c.clamp({c.boundedW() ? c.maxW : ctx.presentation.pt(200), height});
	}
	void paint(Frame &frame) override
	{
		const auto &p = frame.layout.theme.palette;
		const auto &mt = frame.layout.metrics;
		const auto &m = frame.canvas.measurer();
		if (!options.caption.empty())
			frame.canvas.text({bounds.x, bounds.y}, FontRole::Body, ellipsize(m, FontRole::Body, options.caption, bounds.w * 2 / 3), inkFor(frame, options.enabled, false));
		if (!options.valueText.empty())
			frame.canvas.text({bounds.right() - m.width(FontRole::Body, options.valueText), bounds.y}, FontRole::Body, options.valueText, inkFor(frame, options.enabled, true));
		const Rect t = track();
		frame.canvas.fillRounded(t, t.h / 2, p.line);
		const double fraction = maximum > minimum ? double(value - minimum) / (maximum - minimum) : 0;
		const int pos = int(std::lround(fraction * t.w));
		frame.canvas.fillRounded({t.x, t.y, std::max(0, pos), t.h}, t.h / 2, p.muted);
		const Rect thumb{t.x + pos - thumbWidth / 2, t.y + t.h / 2 - thumbWidth / 2 - mt.halfGap / 2, thumbWidth, thumbWidth + mt.halfGap};
		frame.canvas.fillRounded(thumb, thumbWidth / 3, options.enabled ? p.accent : p.disabled);
		frame.canvas.strokeRect(thumb, p.ink.applyAlpha(120));
	}

  private:
	int value, minimum, maximum, thumbWidth = 12, trackHeight = 4;
	std::function<void(int)> change;
	SliderOptions options;
};

std::string masked(const std::string &text)
{
	std::string out;
	for (std::size_t at = 0; at < text.size(); at = nextGlyph(text, at))
		out += '*';
	return out;
}

class TextField : public Node
{
  public:
	TextField(std::string key, std::string value, std::function<void(const std::string &)> change, TextFieldOptions options)
		: value(std::move(value)), change(std::move(change)), options(std::move(options))
	{
		this->key = std::move(key);
		cursor = this->value.size();
	}
	// Drafted fields show the draft while editing, the model value otherwise.
	void adoptDraft(const NodeState &state)
	{
		if (options.commitOnSubmit && state.editing)
		{
			value = state.text;
			editing = true;
		}
		selectAll = state.highlight == 1;
	}
	const char *name() const override { return "textfield"; }
	bool autoFocus() const { return options.autoFocus; }
	std::string accessibleText() const override { return options.password ? masked(value) : value; }
	bool interactive() const override { return true; }
	bool enabled() const override { return options.enabled; }
	bool stateful() const override { return true; }
	void restore(const NodeState &state, const LayoutContext &) override
	{
		adoptDraft(state);
		cursor = std::min(state.cursor, value.size());
		wantsFocus = options.autoFocus && !state.editing && state.detail == 0;
	}
	void save(NodeState &state) const override
	{
		state.cursor = cursor;
		state.editing = editing;
		state.text = value;
		state.highlight = selectAll ? 1 : 0;
		state.detail = 1;
	}
	void tap(Point, Host &host) override
	{
		if (!options.enabled)
			return;
		editing = true;
		auto &state = host.state(key);
		state.editing = true;
		state.text = value;
		host.beginEditing(key);
	}
	void blur(Host &host, bool cancelled) override
	{
		editing = false;
		selectAll = false;
		auto &state = host.state(key);
		state.editing = false;
		state.highlight = 0;
		if (options.commitOnSubmit && !cancelled && change && state.text != committed)
		{
			auto callback = change;
			host.invalidate();
			callback(state.text);
		}
		host.invalidate();
	}
	void activate(Host &host, int direction) override
	{
		if (direction == 0)
			tap(bounds.center(), host);
	}
	void commit(Host &host, std::string next, std::size_t nextCursor)
	{
		if (options.maxLength && glyphCount(next) > options.maxLength)
			return;
		cursor = std::min(nextCursor, next.size());
		auto &state = host.state(key);
		state.cursor = cursor;
		state.highlight = 0;
		selectAll = false;
		if (options.commitOnSubmit)
		{
			value = next;
			state.text = next;
			state.editing = true;
			host.relayout();
			return;
		}
		auto callback = change;
		host.invalidate();
		if (callback)
			callback(next);
	}
	bool textInput(const std::string &text, Host &host) override
	{
		std::string next = selectAll ? std::string() : value;
		if (selectAll)
			cursor = 0;
		next.insert(std::min(cursor, next.size()), text);
		commit(host, next, cursor + text.size());
		return true;
	}
	bool keyDown(const KeyEvent &event, Host &host) override
	{
		cursor = std::min(cursor, value.size());
		switch (event.sym)
		{
		case SDLK_LEFT:
			cursor = previousGlyph(value, cursor);
			host.state(key).cursor = cursor;
			return true;
		case SDLK_RIGHT:
			cursor = nextGlyph(value, cursor);
			host.state(key).cursor = cursor;
			return true;
		case SDLK_HOME:
			cursor = 0;
			host.state(key).cursor = cursor;
			return true;
		case SDLK_END:
			cursor = value.size();
			host.state(key).cursor = cursor;
			return true;
		case SDLK_BACKSPACE:
			if (selectAll)
				commit(host, "", 0);
			else if (cursor > 0)
			{
				std::string next = value;
				const auto from = previousGlyph(next, cursor);
				next.erase(from, cursor - from);
				commit(host, next, from);
			}
			return true;
		case SDLK_DELETE:
			if (selectAll)
				commit(host, "", 0);
			else if (cursor < value.size())
			{
				std::string next = value;
				next.erase(cursor, nextGlyph(next, cursor) - cursor);
				commit(host, next, cursor);
			}
			return true;
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
		{
			if (event.repeat)
				return true;
			auto submit = options.submit;
			const std::string current = value;
			host.endEditing();
			host.invalidate();
			if (submit)
				submit(current);
			return true;
		}
		case SDLK_a:
			if (event.ctrl())
			{
				selectAll = !value.empty();
				host.state(key).highlight = selectAll ? 1 : 0;
				return true;
			}
			return false;
		case SDLK_c:
		case SDLK_x:
			if (event.ctrl() && selectAll)
			{
				SDL_SetClipboardText(value.c_str());
				if (event.sym == SDLK_x)
					commit(host, "", 0);
				return true;
			}
			return false;
		case SDLK_v:
			if (event.ctrl() && SDL_HasClipboardText())
			{
				char *clip = SDL_GetClipboardText();
				std::string pasted = clip ? clip : "";
				SDL_free(clip);
				std::erase(pasted, '\n');
				std::erase(pasted, '\r');
				return textInput(pasted, host);
			}
			return false;
		default:
			return false;
		}
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const int natural = ctx.text.width(FontRole::Body, value.empty() ? options.placeholder : accessibleText()) + 2 * ctx.metrics.padding;
		return c.clamp({c.boundedW() ? c.maxW : std::max(natural, ctx.presentation.pt(160)), ctx.metrics.control});
	}
	void paint(Frame &frame) override
	{
		const auto &p = frame.layout.theme.palette;
		const auto &mt = frame.layout.metrics;
		const auto &m = frame.canvas.measurer();
		const bool active = frame.editing(key);
		frame.canvas.fillRounded(bounds, mt.radius, options.enabled ? p.field : p.disabled);
		frame.canvas.strokeRect(bounds, active ? p.ink : p.line);
		const Rect inner = bounds.inset(Insets::symmetric(mt.gap, 0));
		std::string shown = accessibleText();
		if (active && !frame.composition.empty() && !options.password)
			shown.insert(std::min(cursor, shown.size()), frame.composition);
		const int lineHeight = m.lineHeight(FontRole::Body);
		const int y = textTop(inner, lineHeight);
		if (auto *surface = frame.canvas.surface(); surface && active)
		{
			const Rect clip = frame.canvas.clip();
			SDL_Rect area{bounds.x, bounds.y, bounds.w, bounds.h};
			SDL_Rect limit{clip.x, clip.y, clip.w, clip.h};
			// Browser hosts edit natively; native hosts ignore this.
			GAGCore::browserTextInput(this->browserOwner, area, surface->getW(), surface->getH(), value, options.password, options.maxLength, browserChange, &limit);
		}
		if (shown.empty() && !active)
		{
			frame.canvas.text({inner.x, y}, FontRole::Body, ellipsize(m, FontRole::Body, options.placeholder, inner.w), p.muted);
			return;
		}
		// Keep the cursor visible by shifting long text left.
		const std::size_t at = std::min(cursor, shown.size());
		const int cursorX = m.width(FontRole::Body, shown.substr(0, at));
		int shift = 0;
		if (cursorX > inner.w - mt.gap)
			shift = cursorX - (inner.w - mt.gap);
		frame.canvas.pushClip(inner);
		if (active && selectAll)
			frame.canvas.fillRect({inner.x - shift, y, m.width(FontRole::Body, shown), lineHeight}, p.selected);
		frame.canvas.text({inner.x - shift, y}, FontRole::Body, shown, inkFor(frame, options.enabled, false));
		if (active && (frame.tick / 500) % 2 == 0)
			frame.canvas.fillRect({inner.x - shift + cursorX, y, std::max(1, frame.layout.presentation.pt(1)), lineHeight}, p.ink);
		frame.canvas.popClip();
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		Node::arrange(ctx, rect);
	}
	// Called by the host after restore so autofocus can start editing once.
	bool wantsFocus = false;
	const void *browserOwner = nullptr;
	GAGCore::BrowserTextChange browserChange;
	void bind(Host &host)
	{
		auto &state = host.state(key);
		committed = value;
		adoptDraft(state);
		cursor = std::min(cursor, value.size());
		browserOwner = &state;
		auto callback = change;
		auto submit = options.submit;
		const std::string k = key;
		Host *h = &host;
		browserChange = [h, k, callback, submit](const std::string &text, std::size_t at, int action)
		{
			h->state(k).cursor = at;
			h->invalidate();
			if (callback)
				callback(text);
			if (action && submit)
				submit(text);
		};
	}

  private:
	std::string value, committed;
	std::function<void(const std::string &)> change;
	TextFieldOptions options;
	std::size_t cursor = 0;
	bool editing = false, selectAll = false;
};

class TextEditor : public Node
{
  public:
	TextEditor(std::string key, std::string value, std::function<void(const std::string &)> change, TextEditorOptions options)
		: value(std::move(value)), change(std::move(change)), options(std::move(options))
	{
		this->key = std::move(key);
		cursor = this->value.size();
	}
	const char *name() const override { return "texteditor"; }
	std::string accessibleText() const override { return value; }
	bool interactive() const override { return !options.readOnly; }
	bool focusable() const override { return true; }
	bool stateful() const override { return true; }
	bool scrollable() const override { return true; }
	bool clipsChildren() const override { return true; }
	int scrollOffset() const override { return offset; }
	int scrollMaximum() const override { return maximum; }
	void restore(const NodeState &state, const LayoutContext &) override
	{
		cursor = std::min(state.cursor, value.size());
		offset = state.scroll;
		restored = state.detail != 0;
	}
	void save(NodeState &state) const override
	{
		state.cursor = cursor;
		state.scroll = offset;
		state.detail = 1;
	}
	void scrollBy(int pixels, Host &host) override
	{
		const int next = std::clamp(offset + pixels, 0, maximum);
		if (next != offset)
		{
			offset = next;
			host.relayout();
		}
	}
	void tap(Point point, Host &host) override
	{
		if (options.readOnly)
			return;
		host.beginEditing(key);
		// Place the cursor at the tapped glyph.
		const auto &m = *measurerForHit;
		const int lineHeight = m.lineHeight(FontRole::Body) + lineGap;
		const int line = std::clamp((point.y - bounds.y - pad + offset) / std::max(1, lineHeight), 0, int(lines.size()) - 1);
		if (lines.empty())
		{
			cursor = 0;
			return;
		}
		const auto &text = lines[std::size_t(line)].text;
		std::size_t pos = 0;
		const int x = point.x - bounds.x - pad;
		while (pos < text.size())
		{
			const auto next = nextGlyph(text, pos);
			if (m.width(FontRole::Body, text.substr(0, next)) > x)
				break;
			pos = next;
		}
		cursor = lines[std::size_t(line)].start + pos;
		host.state(key).cursor = cursor;
	}
	void commit(Host &host, std::string next, std::size_t nextCursor)
	{
		cursor = std::min(nextCursor, next.size());
		host.state(key).cursor = cursor;
		follow = true;
		auto callback = change;
		host.invalidate();
		if (callback)
			callback(next);
	}
	bool textInput(const std::string &text, Host &host) override
	{
		if (options.readOnly)
			return false;
		std::string next = value;
		next.insert(std::min(cursor, next.size()), text);
		commit(host, next, cursor + text.size());
		return true;
	}
	std::size_t lineOf(std::size_t at) const
	{
		std::size_t index = 0;
		for (std::size_t i = 0; i < lines.size(); ++i)
			if (lines[i].start <= at)
				index = i;
		return index;
	}
	bool keyDown(const KeyEvent &event, Host &host) override
	{
		if (options.readOnly)
		{
			if (event.sym == SDLK_UP || event.sym == SDLK_DOWN)
			{
				scrollBy((event.sym == SDLK_UP ? -1 : 1) * lineHeightCached, host);
				return true;
			}
			return false;
		}
		cursor = std::min(cursor, value.size());
		auto moveTo = [&](std::size_t at)
		{
			cursor = std::min(at, value.size());
			host.state(key).cursor = cursor;
			follow = true;
			host.relayout();
		};
		switch (event.sym)
		{
		case SDLK_LEFT:
			moveTo(previousGlyph(value, cursor));
			return true;
		case SDLK_RIGHT:
			moveTo(nextGlyph(value, cursor));
			return true;
		case SDLK_UP:
		case SDLK_DOWN:
		{
			if (lines.empty())
				return true;
			const std::size_t line = lineOf(cursor);
			const std::size_t column = cursor - lines[line].start;
			const int target = int(line) + (event.sym == SDLK_UP ? -1 : 1);
			if (target < 0 || target >= int(lines.size()))
				return true;
			const auto &l = lines[std::size_t(target)];
			moveTo(l.start + std::min(column, l.text.size()));
			return true;
		}
		case SDLK_HOME:
			moveTo(lines.empty() ? 0 : lines[lineOf(cursor)].start);
			return true;
		case SDLK_END:
		{
			if (lines.empty())
				return true;
			const auto &l = lines[lineOf(cursor)];
			moveTo(l.start + l.text.size());
			return true;
		}
		case SDLK_PAGEUP:
		case SDLK_PAGEDOWN:
			scrollBy((event.sym == SDLK_PAGEUP ? -1 : 1) * bounds.h, host);
			return true;
		case SDLK_BACKSPACE:
			if (cursor > 0)
			{
				std::string next = value;
				const auto from = previousGlyph(next, cursor);
				next.erase(from, cursor - from);
				commit(host, next, from);
			}
			return true;
		case SDLK_DELETE:
			if (cursor < value.size())
			{
				std::string next = value;
				next.erase(cursor, nextGlyph(next, cursor) - cursor);
				commit(host, next, cursor);
			}
			return true;
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			return textInput("\n", host);
		case SDLK_TAB:
			return false;
		case SDLK_v:
			if (event.ctrl() && SDL_HasClipboardText())
			{
				char *clip = SDL_GetClipboardText();
				std::string pasted = clip ? clip : "";
				SDL_free(clip);
				std::erase(pasted, '\r');
				return textInput(pasted, host);
			}
			return false;
		default:
			return false;
		}
	}
	struct Line
	{
		std::string text;
		std::size_t start;
	};
	// Wrap with byte offsets so cursor and hit tests map back into the value.
	void reflow(const TextMeasurer &m, int width)
	{
		lines.clear();
		const int limit = std::max(1, width);
		std::size_t start = 0;
		while (start <= value.size())
		{
			std::size_t end = start;
			std::size_t lastBreak = std::string::npos;
			while (end < value.size() && value[end] != '\n')
			{
				const std::size_t next = nextGlyph(value, end);
				if (end > start && m.width(FontRole::Body, value.substr(start, next - start)) > limit)
				{
					if (lastBreak != std::string::npos && lastBreak > start)
						end = lastBreak;
					break;
				}
				if (value[end] == ' ')
					lastBreak = end + 1;
				end = next;
			}
			lines.push_back({value.substr(start, end - start), start});
			if (end >= value.size())
				break;
			start = end + (value[end] == '\n' ? 1 : 0);
			if (start > value.size())
				break;
		}
		if (lines.empty())
			lines.push_back({"", 0});
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		pad = ctx.metrics.gap;
		lineGap = ctx.metrics.lineGap / 2;
		const int width = c.boundedW() ? c.maxW : ctx.presentation.pt(320);
		const int lineHeight = ctx.text.lineHeight(FontRole::Body) + lineGap;
		lineHeightCached = lineHeight;
		reflow(ctx.text, width - 2 * pad - ctx.metrics.scrollbar);
		const int natural = std::max(options.lines, 1) * lineHeight + 2 * pad;
		const int content = int(lines.size()) * lineHeight + 2 * pad;
		int height = options.readOnly ? std::min(content, natural) : natural;
		height = std::min(height, c.maxH);
		return c.clamp({width, height});
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		measurerForHit = &ctx.text;
		reflow(ctx.text, rect.w - 2 * pad - ctx.metrics.scrollbar);
		const int lineHeight = ctx.text.lineHeight(FontRole::Body) + lineGap;
		const int content = int(lines.size()) * lineHeight + 2 * pad;
		maximum = std::max(0, content - rect.h);
		if (options.scrollToEnd && !restored)
			offset = maximum;
		else if (options.scrollToEnd && lastContent != content)
			offset = maximum;
		if (follow && !options.readOnly)
		{
			const int line = int(lineOf(cursor));
			const int top = pad + line * lineHeight;
			if (top - offset < 0)
				offset = top;
			else if (top + lineHeight - offset > rect.h - pad)
				offset = top + lineHeight + pad - rect.h;
			follow = false;
		}
		offset = std::clamp(offset, 0, maximum);
		lastContent = content;
		restored = true;
	}
	void paint(Frame &frame) override
	{
		const auto &p = frame.layout.theme.palette;
		const auto &mt = frame.layout.metrics;
		const auto &m = frame.canvas.measurer();
		const bool active = frame.editing(key);
		frame.canvas.fillRounded(bounds, mt.radius, options.readOnly ? p.rail : p.field);
		frame.canvas.strokeRect(bounds, active ? p.ink : p.line);
		if (auto *surface = frame.canvas.surface(); surface && active)
		{
			SDL_Rect area{bounds.x, bounds.y, bounds.w, bounds.h};
			const Rect clip = frame.canvas.clip();
			SDL_Rect limit{clip.x, clip.y, clip.w, clip.h};
			GAGCore::browserTextInput(browserOwner, area, surface->getW(), surface->getH(), value, false, 0, browserChange, &limit);
		}
		const int lineHeight = m.lineHeight(FontRole::Body) + lineGap;
		frame.canvas.pushClip(bounds.inset(1));
		int y = bounds.y + pad - offset;
		for (std::size_t i = 0; i < lines.size(); ++i, y += lineHeight)
		{
			if (y + lineHeight < bounds.y || y > bounds.bottom())
				continue;
			frame.canvas.text({bounds.x + pad, y}, FontRole::Body, lines[i].text, inkFor(frame, true, false));
			if (active && (frame.tick / 500) % 2 == 0 && i == lineOf(cursor))
			{
				const int cx = m.width(FontRole::Body, lines[i].text.substr(0, std::min(cursor - lines[i].start, lines[i].text.size())));
				frame.canvas.fillRect({bounds.x + pad + cx, y, std::max(1, frame.layout.presentation.pt(1)), lineHeight - lineGap}, p.ink);
			}
		}
		frame.canvas.popClip();
		if (maximum > 0)
		{
			const int track = bounds.h - 2;
			const int size = std::max(mt.control / 2, int(double(track) * track / std::max(1, track + maximum)));
			const int top = int(double(track - size) * offset / std::max(1, maximum));
			frame.canvas.fillRounded({bounds.right() - mt.scrollbar - 1, bounds.y + 1 + top, mt.scrollbar, size}, mt.scrollbar / 2, p.muted);
		}
	}
	const void *browserOwner = nullptr;
	GAGCore::BrowserTextChange browserChange;
	void bind(Host &host)
	{
		browserOwner = &host.state(key);
		auto callback = change;
		const std::string k = key;
		Host *h = &host;
		browserChange = [h, k, callback](const std::string &text, std::size_t at, int)
		{
			h->state(k).cursor = at;
			h->invalidate();
			if (callback)
				callback(text);
		};
	}
	bool autoFocus() const { return options.autoFocus && !options.readOnly; }

  private:
	std::string value;
	std::function<void(const std::string &)> change;
	TextEditorOptions options;
	std::vector<Line> lines;
	std::size_t cursor = 0;
	int offset = 0, maximum = 0, pad = 8, lineGap = 2, lineHeightCached = 16, lastContent = -1;
	bool follow = false, restored = false;
	const TextMeasurer *measurerForHit = nullptr;
};

class ListView : public Node
{
  public:
	ListView(std::string key, std::vector<std::string> items, int selected, std::function<void(int)> select, ListOptions options)
		: items(std::move(items)), selected(selected), select(std::move(select)), options(std::move(options))
	{
		this->key = std::move(key);
	}
	const char *name() const override { return "list"; }
	std::string accessibleText() const override
	{
		return selected >= 0 && selected < int(items.size()) ? items[std::size_t(selected)] : std::string();
	}
	bool interactive() const override { return true; }
	bool stateful() const override { return true; }
	bool scrollable() const override { return true; }
	bool clipsChildren() const override { return true; }
	int scrollOffset() const override { return offset; }
	int scrollMaximum() const override { return maximum; }
	void restore(const NodeState &state, const LayoutContext &) override
	{
		offset = state.scroll;
		revealed = state.detail == selected + 1;
	}
	void save(NodeState &state) const override
	{
		state.scroll = offset;
		state.detail = selected + 1;
	}
	void scrollBy(int pixels, Host &host) override
	{
		const int next = std::clamp(offset + pixels, 0, maximum);
		if (next != offset)
		{
			offset = next;
			host.relayout();
		}
	}
	int rowAt(Point point) const
	{
		if (rowHeight <= 0)
			return -1;
		const int index = (point.y - bounds.y + offset) / rowHeight;
		return index >= 0 && index < int(items.size()) ? index : -1;
	}
	bool rowEnabled(int index) const { return options.enabled.empty() || options.enabled[std::size_t(index)]; }
	void choose(Host &host, int index)
	{
		if (index < 0 || index >= int(items.size()) || !rowEnabled(index))
			return;
		auto callback = select;
		host.state(key).detail = index + 1;
		host.invalidate();
		if (callback)
			callback(index);
	}
	void tap(Point point, Host &host) override
	{
		const int index = rowAt(point);
		if (index < 0)
			return;
		const bool checkbox = !options.checked.empty() && point.x < bounds.x + checkWidth;
		if (checkbox || (!options.checked.empty() && index == selected))
		{
			if (options.toggle && rowEnabled(index))
			{
				auto callback = options.toggle;
				const bool next = !options.checked[std::size_t(index)];
				host.invalidate();
				callback(index, next);
			}
			return;
		}
		choose(host, index);
	}
	void activate(Host &host, int direction) override
	{
		if (direction == 0)
		{
			if (selected >= 0 && options.activate)
			{
				auto callback = options.activate;
				host.invalidate();
				callback(selected);
			}
			else if (selected >= 0 && options.toggle && !options.checked.empty())
			{
				auto callback = options.toggle;
				const bool next = !options.checked[std::size_t(selected)];
				host.invalidate();
				callback(selected, next);
			}
		}
	}
	bool keyDown(const KeyEvent &event, Host &host) override
	{
		if (event.sym != SDLK_UP && event.sym != SDLK_DOWN)
			return false;
		if (items.empty())
			return true;
		int next = selected < 0 ? (event.sym == SDLK_DOWN ? 0 : int(items.size()) - 1)
								: std::clamp(selected + (event.sym == SDLK_UP ? -1 : 1), 0, int(items.size()) - 1);
		if (next != selected)
			choose(host, next);
		return true;
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		rowHeight = std::max(ctx.metrics.control, ctx.text.lineHeight(FontRole::Body) + ctx.metrics.gap);
		checkWidth = options.checked.empty() ? 0 : ctx.metrics.control;
		const int rows = std::max(1, options.visibleRows);
		int width = c.boundedW() ? c.maxW : 0;
		if (!c.boundedW())
			for (const auto &item : items)
				width = std::max(width, ctx.text.width(FontRole::Body, item) + 2 * ctx.metrics.gap + checkWidth + ctx.metrics.scrollbar);
		// Natural height shows `visibleRows`; a flex parent passes tight constraints to fill.
		const int height = std::min(rows * rowHeight, c.maxH);
		return c.clamp({width, height});
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		maximum = std::max(0, int(items.size()) * rowHeight - rect.h);
		if (!revealed && selected >= 0)
		{
			const int top = selected * rowHeight;
			if (top < offset)
				offset = top;
			else if (top + rowHeight > offset + rect.h)
				offset = top + rowHeight - rect.h;
			revealed = true;
		}
		offset = std::clamp(offset, 0, maximum);
	}
	void paint(Frame &frame) override
	{
		const auto &p = frame.layout.theme.palette;
		const auto &mt = frame.layout.metrics;
		const auto &m = frame.canvas.measurer();
		frame.canvas.fillRounded(bounds, mt.radius, p.field);
		frame.canvas.strokeRect(bounds, p.line);
		frame.canvas.pushClip(bounds.inset(1));
		const int barSpace = maximum > 0 ? mt.scrollbar + mt.halfGap : 0;
		if (items.empty() && !options.emptyText.empty())
			frame.canvas.text({bounds.x + mt.gap, bounds.y + mt.gap}, FontRole::Body, ellipsize(m, FontRole::Body, options.emptyText, bounds.w - 2 * mt.gap), p.muted);
		for (int i = 0; i < int(items.size()); ++i)
		{
			const Rect row{bounds.x + 1, bounds.y + i * rowHeight - offset, bounds.w - 2 - barSpace, rowHeight};
			if (row.bottom() < bounds.y || row.y > bounds.bottom())
				continue;
			const bool ok = rowEnabled(i);
			if (i == selected)
				frame.canvas.fillRect(row, p.selected);
			else if (ok && frame.hovered(row))
				frame.canvas.fillRect(row, p.hover.applyAlpha(50));
			int x = row.x + mt.gap;
			if (!options.checked.empty())
			{
				const int side = std::min(rowHeight - mt.gap, frame.layout.presentation.pt(18));
				const Rect mark{row.x + (checkWidth - side) / 2, row.y + (row.h - side) / 2, side, side};
				frame.canvas.fillRounded(mark, 2, options.checked[std::size_t(i)] ? p.accent : p.paper);
				frame.canvas.strokeRect(mark, ok ? p.ink : p.muted);
				if (options.checked[std::size_t(i)])
					for (int t = 0; t < 2; ++t)
					{
						frame.canvas.line({mark.x + side / 5, mark.y + side / 2 + t}, {mark.x + side * 2 / 5, mark.y + side * 3 / 4 + t}, p.ink);
						frame.canvas.line({mark.x + side * 2 / 5, mark.y + side * 3 / 4 + t}, {mark.x + side * 4 / 5, mark.y + side / 4 + t}, p.ink);
					}
				x = row.x + checkWidth;
			}
			if (options.paintRow)
				options.paintRow(frame.canvas, {x, row.y, std::max(1, row.right() - x), row.h}, i, i == selected);
			else
				frame.canvas.text({x, textTop(row, m.lineHeight(FontRole::Body))}, FontRole::Body, ellipsize(m, FontRole::Body, items[std::size_t(i)], row.right() - x - mt.gap), inkFor(frame, ok, false));
		}
		frame.canvas.popClip();
		if (maximum > 0)
		{
			const int track = bounds.h - 2;
			const int size = std::max(mt.control / 2, int(double(track) * track / std::max(1, track + maximum)));
			const int top = int(double(track - size) * offset / std::max(1, maximum));
			frame.canvas.fillRounded({bounds.right() - mt.scrollbar - 1, bounds.y + 1, mt.scrollbar, track}, mt.scrollbar / 2, p.line);
			frame.canvas.fillRounded({bounds.right() - mt.scrollbar - 1, bounds.y + 1 + top, mt.scrollbar, size}, mt.scrollbar / 2, p.muted);
		}
	}
	bool capturesPointer(Point point) const override
	{
		return maximum > 0 && point.x >= bounds.right() - lastScrollbar - 6;
	}
	void pointer(PointerPhase phase, Point point, Host &host) override
	{
		if (phase == PointerPhase::Cancel)
			return;
		const int track = std::max(1, bounds.h - 2);
		const int size = std::max(1, int(double(track) * track / std::max(1, track + maximum)));
		if (phase == PointerPhase::Down)
			grab = size / 2;
		const int top = point.y - bounds.y - 1 - grab;
		offset = std::clamp(int(std::lround(double(top) * maximum / std::max(1, track - size))), 0, maximum);
		host.relayout();
	}
	void cacheScrollbar(int width) { lastScrollbar = width; }

  private:
	std::vector<std::string> items;
	int selected;
	std::function<void(int)> select;
	ListOptions options;
	int rowHeight = 24, checkWidth = 0, offset = 0, maximum = 0, grab = 0, lastScrollbar = 6;
	bool revealed = false;
};

class ListArranged : public ListView
{
  public:
	using ListView::ListView;
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		ListView::arrange(ctx, rect);
		cacheScrollbar(ctx.metrics.scrollbar);
	}
};

class Progress : public Node
{
  public:
	Progress(int value, int range, std::string text) : value(value), range(range), text(std::move(text)) {}
	const char *name() const override { return "progress"; }
	std::string accessibleText() const override { return text; }
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		return c.clamp({c.boundedW() ? c.maxW : ctx.presentation.pt(200), std::max(ctx.presentation.pt(14), text.empty() ? 0 : ctx.text.lineHeight(FontRole::Support) + ctx.metrics.halfGap)});
	}
	void paint(Frame &frame) override
	{
		const auto &p = frame.layout.theme.palette;
		const int radius = frame.layout.metrics.radius;
		frame.canvas.fillRounded(bounds, radius, p.rail);
		if (range > 0)
			frame.canvas.fillRounded({bounds.x, bounds.y, int(std::lround(double(bounds.w) * std::clamp(value, 0, range) / range)), bounds.h}, radius, p.accent);
		if (!text.empty())
			drawLines(frame, bounds, FontRole::Support, {text}, TextAlign::Center, p.ink, 0);
	}

  private:
	int value, range;
	std::string text;
};

class Image : public Node
{
  public:
	Image(GAGCore::DrawableSurface *surface, ImageOptions options) : surface(surface), options(options) {}
	const char *name() const override { return "image"; }
	Size natural() const
	{
		if (options.size)
			return *options.size;
		return surface ? Size{surface->getW(), surface->getH()} : Size{};
	}
	Size measure(const LayoutContext &, Constraints c) override
	{
		Size s = natural();
		if (options.fit && s.w > 0 && s.h > 0)
		{
			double scale = 1;
			if (c.boundedW() && s.w > c.maxW)
				scale = std::min(scale, double(c.maxW) / s.w);
			if (c.boundedH() && s.h > c.maxH)
				scale = std::min(scale, double(c.maxH) / s.h);
			s = {int(s.w * scale), int(s.h * scale)};
		}
		return c.clamp(s);
	}
	void paint(Frame &frame) override
	{
		if (!surface)
			return;
		Size s = natural();
		Rect dest = bounds;
		if (options.fit && s.w > 0 && s.h > 0)
		{
			const double scale = std::min(double(bounds.w) / s.w, double(bounds.h) / s.h);
			dest = {bounds.x + (bounds.w - int(s.w * scale)) / 2, bounds.y + (bounds.h - int(s.h * scale)) / 2, int(s.w * scale), int(s.h * scale)};
		}
		frame.canvas.drawSurface(dest, surface);
	}

  private:
	GAGCore::DrawableSurface *surface;
	ImageOptions options;
};

class SpriteNode : public Node
{
  public:
	SpriteNode(GAGCore::Sprite *sprite, int frame, std::optional<Size> size) : sprite(sprite), frameIndex(frame), size(size) {}
	const char *name() const override { return "sprite"; }
	Size measure(const LayoutContext &, Constraints c) override
	{
		if (size)
			return c.clamp(*size);
		return c.clamp(sprite ? Size{sprite->getW(frameIndex), sprite->getH(frameIndex)} : Size{});
	}
	void paint(Frame &frame) override
	{
		if (!sprite)
			return;
		const int w = sprite->getW(frameIndex), h = sprite->getH(frameIndex);
		frame.canvas.pushClip(bounds);
		frame.canvas.drawSprite({bounds.x + (bounds.w - w) / 2, bounds.y + (bounds.h - h) / 2}, sprite, frameIndex);
		frame.canvas.popClip();
	}

  private:
	GAGCore::Sprite *sprite;
	int frameIndex;
	std::optional<Size> size;
};

class Swatch : public Node
{
  public:
	Swatch(GAGCore::Color color, int points) : color(color), points(points) {}
	const char *name() const override { return "swatch"; }
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const int side = ctx.presentation.pt(points);
		return c.clamp({side, side});
	}
	void paint(Frame &frame) override
	{
		frame.canvas.fillRounded(bounds, std::max(2, frame.layout.metrics.radius / 2), color);
		frame.canvas.strokeRect(bounds, frame.layout.theme.palette.ink.applyAlpha(90));
	}

  private:
	GAGCore::Color color;
	int points;
};

class CanvasNode : public Node
{
  public:
	CanvasNode(std::string key, Size preferred, std::function<void(Canvas &, Rect, const Frame &)> painter, CanvasOptions options)
		: preferred(preferred), painter(std::move(painter)), options(std::move(options))
	{
		this->key = std::move(key);
	}
	const char *name() const override { return "canvas"; }
	std::string accessibleText() const override { return options.accessibleText; }
	bool interactive() const override { return bool(options.tap) || bool(options.pointer); }
	bool focusable() const override { return options.focusable && interactive(); }
	bool capturesPointer(Point) const override { return bool(options.pointer); }
	bool scrollable() const override { return bool(options.wheel); }
	void scrollBy(int pixels, Host &host) override
	{
		if (options.wheel)
		{
			options.wheel(pixels < 0 ? 1 : -1, {lastHover.x - bounds.x, lastHover.y - bounds.y});
			host.relayout();
		}
	}
	void tap(Point point, Host &host) override
	{
		if (options.tap)
			options.tap({point.x - bounds.x, point.y - bounds.y}, host);
	}
	void pointer(PointerPhase phase, Point point, Host &host) override
	{
		if (options.pointer)
			options.pointer(phase, {point.x - bounds.x, point.y - bounds.y}, host);
	}
	Size measure(const LayoutContext &, Constraints c) override
	{
		Size s = preferred;
		if (flex > 0)
			s = c.biggest();
		else if (options.keepAspect && preferred.w > 0 && preferred.h > 0)
		{
			double scale = 1;
			if (c.boundedW() && s.w > c.maxW)
				scale = std::min(scale, double(c.maxW) / s.w);
			if (c.boundedH() && s.h > c.maxH)
				scale = std::min(scale, double(c.maxH) / s.h);
			s = {int(s.w * scale), int(s.h * scale)};
		}
		return c.clamp(s);
	}
	void paint(Frame &frame) override
	{
		if (frame.hoverValid)
			lastHover = frame.hover;
		if (options.hover && frame.hovered(bounds))
			options.hover({frame.hover.x - bounds.x, frame.hover.y - bounds.y});
		frame.canvas.pushClip(bounds);
		if (painter)
			painter(frame.canvas, bounds, frame);
		frame.canvas.popClip();
	}

  private:
	Size preferred;
	std::function<void(Canvas &, Rect, const Frame &)> painter;
	CanvasOptions options;
	Point lastHover;
};
} // namespace

Element label(const std::string &text, TextOptions options) { return std::make_shared<Label>(text, options); }
Element heading(const std::string &text) { return std::make_shared<Paragraph>(text, TextOptions{FontRole::Heading}); }
Element title(const std::string &text) { return std::make_shared<Paragraph>(text, TextOptions{FontRole::Title}); }
Element caption(const std::string &text, bool muted) { return std::make_shared<Paragraph>(text, TextOptions{FontRole::Support, muted}); }
Element paragraph(const std::string &text, TextOptions options) { return std::make_shared<Paragraph>(text, options); }
Element button(const std::string &key, const std::string &text, std::function<void()> action, ButtonOptions options)
{
	return std::make_shared<Button>(key, text, std::move(action), options);
}
Element toggle(const std::string &key, const std::string &text, bool value, std::function<void(bool)> change, bool enabled)
{
	return std::make_shared<Toggle>(key, text, value, std::move(change), enabled);
}
Element choice(const std::string &key, const std::vector<std::string> &options, int selected, std::function<void(int)> change, ChoiceOptions extra)
{
	return std::make_shared<Choice>(key, options, selected, std::move(change), std::move(extra));
}
Element chooser(const std::string &key, const std::string &value, std::function<void()> open, bool enabled)
{
	return std::make_shared<Chooser>(key, value, std::move(open), enabled);
}
Element segments(const std::string &key, const std::vector<std::string> &options, int selected, std::function<void(int)> change, std::vector<bool> enabled)
{
	std::vector<Element> parts;
	for (std::size_t i = 0; i < options.size(); ++i)
	{
		ButtonOptions b;
		b.selected = int(i) == selected;
		b.enabled = enabled.empty() || enabled[i];
		parts.push_back(expanded(button(key + "/" + std::to_string(i), options[i], [change, i] { if (change) change(int(i)); }, b)));
	}
	return row(std::move(parts), {-1, CrossAlign::Stretch});
}
Element stepper(const std::string &key, int value, int minimum, int maximum, std::function<void(int)> change, StepperOptions options)
{
	return std::make_shared<Stepper>(key, value, minimum, maximum, std::move(change), options);
}
Element slider(const std::string &key, int value, int minimum, int maximum, std::function<void(int)> change, SliderOptions options)
{
	return std::make_shared<Slider>(key, value, minimum, maximum, std::move(change), options);
}
Element textField(const std::string &key, const std::string &value, std::function<void(const std::string &)> change, TextFieldOptions options)
{
	return std::make_shared<TextField>(key, value, std::move(change), std::move(options));
}
Element textEditor(const std::string &key, const std::string &value, std::function<void(const std::string &)> change, TextEditorOptions options)
{
	return std::make_shared<TextEditor>(key, value, std::move(change), std::move(options));
}
Element listView(const std::string &key, const std::vector<std::string> &items, int selected, std::function<void(int)> select, ListOptions options)
{
	return std::make_shared<ListArranged>(key, items, selected, std::move(select), std::move(options));
}
Element progress(int value, int range, const std::string &text) { return std::make_shared<Progress>(value, range, text); }
Element image(GAGCore::DrawableSurface *surface, ImageOptions options) { return std::make_shared<Image>(surface, options); }
Element sprite(GAGCore::Sprite *sprite, int frame, std::optional<Size> size) { return std::make_shared<SpriteNode>(sprite, frame, size); }
Element swatch(GAGCore::Color color, int points) { return std::make_shared<Swatch>(color, points); }
Element canvas(const std::string &key, Size preferred, std::function<void(Canvas &, Rect, const Frame &)> paint, CanvasOptions options)
{
	return std::make_shared<CanvasNode>(key, preferred, std::move(paint), std::move(options));
}

// Host hooks that need the concrete types above.
void bindTextControls(Host &host, Node &root)
{
	root.visit(
		[&](Node &node)
		{
			if (auto *field = dynamic_cast<TextField *>(&node))
			{
				field->bind(host);
				if (field->autoFocus() && host.editing().empty() && host.state(field->key).detail == 0)
				{
					host.state(field->key).detail = 1;
					host.beginEditing(field->key);
				}
			}
			else if (auto *editor = dynamic_cast<TextEditor *>(&node))
			{
				editor->bind(host);
				if (editor->autoFocus() && host.editing().empty() && host.state(editor->key).detail == 0)
				{
					host.state(editor->key).detail = 1;
					host.beginEditing(editor->key);
				}
			}
		});
}
} // namespace GAGGUI::ui
