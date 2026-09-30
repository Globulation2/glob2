// SPDX-License-Identifier: GPL-3.0-or-later
#include <ui/Containers.h>
#include <ui/Controls.h>
#include <ui/Host.h>
#include <algorithm>
#include <cmath>

namespace GAGGUI::ui
{
namespace
{
void appendChildren(Node &node, std::vector<Element> &children)
{
	for (auto &child : children)
		if (child)
			node.children.push_back(std::move(child));
}

int resolveGap(const LayoutContext &ctx, int gap) { return gap < 0 ? ctx.metrics.gap : gap; }

// Restore framework state for nodes created during layout (Adaptive/Field).
void restoreSubtree(const LayoutContext &ctx, Node &node)
{
	if (!ctx.states)
		return;
	node.visit(
		[&](Node &n)
		{
			if (n.stateful() && !n.key.empty())
				if (const auto *state = ctx.states->find(n.key))
					n.restore(*state, ctx);
		});
}

class Axis : public Node
{
  public:
	Axis(bool vertical, std::vector<Element> items, StackOptions options)
		: vertical(vertical), options(options)
	{
		appendChildren(*this, items);
	}
	const char *name() const override { return vertical ? "column" : "row"; }
	int main(Size s) const { return vertical ? s.h : s.w; }
	int cross(Size s) const { return vertical ? s.w : s.h; }
	Size make(int mainValue, int crossValue) const
	{
		return vertical ? Size{crossValue, mainValue} : Size{mainValue, crossValue};
	}
	int maxMain(Constraints c) const { return vertical ? c.maxH : c.maxW; }
	int maxCross(Constraints c) const { return vertical ? c.maxW : c.maxH; }
	bool boundedMain(Constraints c) const { return vertical ? c.boundedH() : c.boundedW(); }
	bool boundedCross(Constraints c) const { return vertical ? c.boundedW() : c.boundedH(); }

	// Sizes every child; returns the total main extent (including gaps).
	std::vector<Size> sizeChildren(const LayoutContext &ctx, Constraints c, int &total,
								   int &crossExtent)
	{
		const int gap = resolveGap(ctx, options.gap);
		std::vector<Size> sizes(children.size());
		int fixed = 0, flexTotal = 0;
		const int crossLimit = boundedCross(c) ? maxCross(c) : Constraints::Unbounded;
		Constraints childConstraints;
		if (vertical)
			childConstraints = {0, 0, crossLimit, Constraints::Unbounded};
		else
			childConstraints = {0, 0, Constraints::Unbounded, crossLimit};
		// Stretch applies when arranging: children are measured at their natural cross
		// size so a row never claims the whole height its column has left over.
		const int gaps = children.empty() ? 0 : int(children.size() - 1) * gap;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			if (children[i]->flex > 0)
			{
				flexTotal += children[i]->flex;
				continue;
			}
			// In a column, natural-size children may not exceed what remains of a
			// bounded height, so a scroll region shrinks to content yet never
			// overflows. Rows keep unbounded widths so wrapping action groups and
			// previews report their natural width beside flexible siblings.
			Constraints c2 = childConstraints;
			if (vertical && boundedMain(c))
				c2.maxH = std::max(0, maxMain(c) - fixed - gaps);
			sizes[i] = children[i]->measure(ctx, c2);
			fixed += main(sizes[i]);
		}
		if (flexTotal > 0)
		{
			const bool bounded = boundedMain(c);
			int remaining = bounded ? std::max(0, maxMain(c) - fixed - gaps) : 0;
			int used = 0, assigned = 0;
			for (std::size_t i = 0; i < children.size(); ++i)
			{
				if (children[i]->flex <= 0)
					continue;
				++assigned;
				Constraints fc = childConstraints;
				if (bounded)
				{
					const int share = assigned == flexTotal ? remaining - used
															: remaining * children[i]->flex / flexTotal;
					used += share;
					if (vertical)
						fc.minH = fc.maxH = share;
					else
						fc.minW = fc.maxW = share;
				}
				sizes[i] = children[i]->measure(ctx, fc);
				fixed += main(sizes[i]);
			}
		}
		total = fixed + gaps;
		crossExtent = vertical ? c.minW : c.minH;
		for (const auto &s : sizes)
			crossExtent = std::max(crossExtent, cross(s));
		return sizes;
	}

	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		int total = 0, crossExtent = 0;
		sizeChildren(ctx, c, total, crossExtent);
		return c.clamp(make(total, crossExtent));
	}

	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		const int gap = resolveGap(ctx, options.gap);
		int total = 0, crossExtent = 0;
		auto sizes = sizeChildren(ctx, Constraints::loose(rect.size()), total, crossExtent);
		const int mainAvailable = main(rect.size());
		int offset = 0, extraGap = 0;
		const int slack = std::max(0, mainAvailable - total);
		if (options.main == MainAlign::Center)
			offset = slack / 2;
		else if (options.main == MainAlign::End)
			offset = slack;
		else if (options.main == MainAlign::SpaceBetween && children.size() > 1)
			extraGap = slack / int(children.size() - 1);
		int cursor = offset;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const int mainSize = main(sizes[i]);
			int crossSize = cross(sizes[i]);
			const int crossAvailable = cross(rect.size());
			int crossOffset = 0;
			if (options.cross == CrossAlign::Stretch)
				crossSize = crossAvailable;
			else if (options.cross == CrossAlign::Center)
				crossOffset = (crossAvailable - crossSize) / 2;
			else if (options.cross == CrossAlign::End)
				crossOffset = crossAvailable - crossSize;
			Rect child = vertical ? Rect{rect.x + crossOffset, rect.y + cursor, crossSize, mainSize}
								  : Rect{rect.x + cursor, rect.y + crossOffset, mainSize, crossSize};
			children[i]->arrange(ctx, child);
			cursor += mainSize + gap + extraGap;
		}
	}

  private:
	bool vertical;
	StackOptions options;
};

class Overlay : public Node
{
  public:
	explicit Overlay(std::vector<Element> items) { appendChildren(*this, items); }
	const char *name() const override { return "stack"; }
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		Size result;
		for (auto &child : children)
		{
			const Size s = child->measure(ctx, c);
			result.w = std::max(result.w, s.w);
			result.h = std::max(result.h, s.h);
		}
		return c.clamp(result);
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		for (auto &child : children)
			child->arrange(ctx, rect);
	}
};

class Padding : public Wrapper
{
  public:
	Padding(Insets insets, Element child, bool themed = false)
		: Wrapper(std::move(child)), insets(insets), themed(themed)
	{
	}
	const char *name() const override { return "padding"; }
	Insets resolved(const LayoutContext &ctx) const
	{
		return themed ? Insets::all(ctx.metrics.padding) : insets;
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const auto i = resolved(ctx);
		Size inner = child() ? child()->measure(ctx, c.deflate(i)) : Size{};
		return c.clamp({inner.w + i.horizontal(), inner.h + i.vertical()});
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		if (child())
			child()->arrange(ctx, rect.inset(resolved(ctx)));
	}

  private:
	Insets insets;
	bool themed;
};

class Align : public Wrapper
{
  public:
	Align(Alignment alignment, Element child) : Wrapper(std::move(child)), alignment(alignment) {}
	const char *name() const override { return "align"; }
	// Shrink-wraps the child; only a minimum from the parent (a flex share, a
	// tight box) makes the alignment area larger than its content.
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const Size inner = child() ? child()->measure(ctx, c.loosen()) : Size{};
		return c.clamp({std::max(inner.w, c.minW), std::max(inner.h, c.minH)});
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		if (!child())
			return;
		const Size inner = child()->measure(ctx, Constraints::loose(rect.size()));
		const int column = int(alignment) % 3, line = int(alignment) / 3;
		const int x = rect.x + (rect.w - inner.w) * column / 2;
		const int y = rect.y + (rect.h - inner.h) * line / 2;
		child()->arrange(ctx, {x, y, inner.w, inner.h});
	}

  private:
	Alignment alignment;
};

class Constrained : public Wrapper
{
  public:
	Constrained(Constraints limits, Element child) : Wrapper(std::move(child)), limits(limits) {}
	const char *name() const override { return "constrained"; }
	Constraints merge(Constraints c) const
	{
		Constraints out = c;
		out.minW = std::max(c.minW, std::min(limits.minW, c.maxW));
		out.minH = std::max(c.minH, std::min(limits.minH, c.maxH));
		out.maxW = std::max(out.minW, std::min(c.maxW, limits.maxW));
		out.maxH = std::max(out.minH, std::min(c.maxH, limits.maxH));
		return out;
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const auto merged = merge(c);
		const Size inner = child() ? child()->measure(ctx, merged) : Size{};
		return c.clamp(merged.clamp(inner));
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		if (child())
			child()->arrange(ctx, rect);
	}

  private:
	Constraints limits;
};

class Spacer : public Node
{
  public:
	explicit Spacer(int size) : size(size) {}
	const char *name() const override { return "spacer"; }
	Size measure(const LayoutContext &, Constraints c) override
	{
		if (flex > 0)
			return c.clamp(c.biggest());
		return c.clamp({size, size});
	}

  private:
	int size;
};

class Divider : public Node
{
  public:
	const char *name() const override { return "divider"; }
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		return c.clamp({c.boundedW() ? c.maxW : 0, std::max(1, ctx.presentation.pt(1))});
	}
	void paint(Frame &frame) override
	{
		frame.canvas.fillRect(bounds, frame.layout.theme.palette.line);
	}
};

class Scroll : public Wrapper
{
  public:
	Scroll(std::string key, Element child, ScrollOptions options)
		: Wrapper(std::move(child)), options(options)
	{
		this->key = std::move(key);
	}
	const char *name() const override { return "scroll"; }
	bool stateful() const override { return true; }
	bool scrollable() const override { return true; }
	bool clipsChildren() const override { return true; }
	void restore(const NodeState &state, const LayoutContext &) override { offset = state.scroll; }
	void save(NodeState &state) const override { state.scroll = offset; }
	int scrollOffset() const override { return offset; }
	int scrollMaximum() const override { return maximum; }
	int barWidth(const LayoutContext &ctx) const { return ctx.metrics.scrollbar + ctx.metrics.halfGap; }

	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		Constraints inner = c.unboundedH();
		Size content = child() ? child()->measure(ctx, inner) : Size{};
		const bool overflows = c.boundedH() && content.h > c.maxH;
		int width = content.w;
		if (overflows || options.alwaysShowBar)
		{
			inner = inner.deflate({0, 0, barWidth(ctx), 0});
			content = child() ? child()->measure(ctx, inner) : Size{};
			width = content.w + barWidth(ctx);
		}
		int height = content.h;
		if (c.boundedH() && (!options.shrinkToContent || overflows))
			height = c.maxH;
		return c.clamp({width, height});
	}

	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		if (!child())
			return;
		Constraints inner{rect.w, 0, rect.w, Constraints::Unbounded};
		Size content = child()->measure(ctx, inner);
		showBar = content.h > rect.h || options.alwaysShowBar;
		if (showBar)
		{
			const int width = std::max(0, rect.w - barWidth(ctx));
			inner = {width, 0, width, Constraints::Unbounded};
			content = child()->measure(ctx, inner);
		}
		contentHeight = content.h;
		maximum = std::max(0, content.h - rect.h);
		offset = std::clamp(offset, 0, maximum);
		child()->arrange(ctx, {rect.x, rect.y - offset, inner.maxW, content.h});
	}

	Rect thumb(const LayoutContext &ctx) const
	{
		const int track = bounds.h;
		const int size = std::max(ctx.metrics.control / 2,
								  int(double(track) * track / std::max(1, contentHeight)));
		const int travel = std::max(0, track - size);
		const int top = maximum > 0 ? int(double(travel) * offset / maximum) : 0;
		return {bounds.right() - ctx.metrics.scrollbar, bounds.y + top, ctx.metrics.scrollbar, size};
	}

	void paint(Frame &frame) override
	{
		frame.canvas.pushClip(bounds);
		Wrapper::paint(frame);
		frame.canvas.popClip();
		if (!showBar || maximum <= 0)
			return;
		const auto &palette = frame.layout.theme.palette;
		const int radius = frame.layout.metrics.scrollbar / 2;
		frame.canvas.fillRounded({bounds.right() - frame.layout.metrics.scrollbar, bounds.y,
								  frame.layout.metrics.scrollbar, bounds.h},
								 radius, palette.line);
		frame.canvas.fillRounded(thumb(frame.layout), radius, palette.muted);
	}

	bool capturesPointer(Point point) const override
	{
		if (!showBar || maximum <= 0)
			return false;
		const Rect track{bounds.right() - lastScrollbar - 6, bounds.y, lastScrollbar + 6, bounds.h};
		return track.contains(point);
	}
	void pointer(PointerPhase phase, Point point, Host &host) override
	{
		if (phase == PointerPhase::Down)
		{
			const Rect t = thumbRect;
			grab = t.contains(point) ? point.y - t.y : t.h / 2;
		}
		if (phase == PointerPhase::Cancel)
			return;
		const int travel = std::max(1, bounds.h - thumbRect.h);
		const int top = point.y - grab - bounds.y;
		offset = std::clamp(int(std::lround(double(top) * maximum / travel)), 0, maximum);
		host.relayout();
	}
	void scrollBy(int pixels, Host &host) override
	{
		const int next = std::clamp(offset + pixels, 0, maximum);
		if (next == offset)
			return;
		offset = next;
		host.relayout();
	}
	// Painter-independent thumb geometry cached at arrange time for hit tests.
	void cacheThumb(const LayoutContext &ctx)
	{
		lastScrollbar = ctx.metrics.scrollbar;
		thumbRect = thumb(ctx);
	}

  private:
	ScrollOptions options;
	int offset = 0, maximum = 0, contentHeight = 0, grab = 0, lastScrollbar = 6;
	bool showBar = false;
	Rect thumbRect;
};

class ScrollArranged : public Scroll
{
  public:
	using Scroll::Scroll;
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		Scroll::arrange(ctx, rect);
		cacheThumb(ctx);
	}
};

class Wrap : public Node
{
  public:
	Wrap(std::vector<Element> items, WrapOptions options) : options(options)
	{
		appendChildren(*this, items);
	}
	const char *name() const override { return "wrap"; }
	int columns(const LayoutContext &ctx, int width) const
	{
		const int gap = resolveGap(ctx, options.gap);
		const int minimum = options.minChildWidth < 0 ? ctx.metrics.dialogMaxWidth / 3
													   : options.minChildWidth;
		int count = std::max(1, (width + gap) / std::max(1, minimum + gap));
		count = std::min(count, std::max(1, int(children.size())));
		if (options.maxColumns > 0)
			count = std::min(count, options.maxColumns);
		return count;
	}
	std::vector<Size> place(const LayoutContext &ctx, int width, std::vector<Rect> *rects,
							int &height)
	{
		const int gap = resolveGap(ctx, options.gap);
		const int cols = columns(ctx, width);
		const int cell = std::max(1, (width - (cols - 1) * gap) / cols);
		std::vector<Size> sizes(children.size());
		height = 0;
		int rowTop = 0, rowHeight = 0;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			Constraints c{options.stretch ? cell : 0, 0, cell, Constraints::Unbounded};
			sizes[i] = children[i]->measure(ctx, c);
			if (i % cols == 0 && i > 0)
			{
				rowTop += rowHeight + gap;
				rowHeight = 0;
			}
			rowHeight = std::max(rowHeight, sizes[i].h);
		}
		height = children.empty() ? 0 : rowTop + rowHeight;
		if (rects)
		{
			rects->assign(children.size(), {});
			rowTop = 0;
			rowHeight = 0;
			for (std::size_t i = 0; i < children.size(); ++i)
			{
				if (i % cols == 0 && i > 0)
				{
					rowTop += rowHeight + gap;
					rowHeight = 0;
				}
				rowHeight = std::max(rowHeight, sizes[i].h);
				(*rects)[i] = {int(i % cols) * (cell + gap), rowTop, options.stretch ? cell : sizes[i].w, sizes[i].h};
			}
			// Stretch rows to their tallest member so buttons align.
			for (std::size_t i = 0; i < children.size(); ++i)
			{
				int tallest = 0;
				const std::size_t start = i - i % cols;
				for (std::size_t j = start; j < children.size() && j < start + cols; ++j)
					tallest = std::max(tallest, (*rects)[j].h);
				(*rects)[i].h = tallest;
			}
		}
		return sizes;
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		int width = c.boundedW() ? c.maxW : 0;
		if (!c.boundedW())
			for (auto &child : children)
				width += child->measure(ctx, c).w + resolveGap(ctx, options.gap);
		int height = 0;
		place(ctx, std::max(1, width), nullptr, height);
		return c.clamp({width, height});
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		std::vector<Rect> rects;
		int height = 0;
		place(ctx, std::max(1, rect.w), &rects, height);
		for (std::size_t i = 0; i < children.size(); ++i)
			children[i]->arrange(ctx, rects[i].translated(rect.x, rect.y));
	}

  private:
	WrapOptions options;
};

class Adaptive : public Node
{
  public:
	explicit Adaptive(std::function<Element(const LayoutContext &, Size)> choose)
		: choose(std::move(choose))
	{
	}
	const char *name() const override { return "adaptive"; }
	Node *ensure(const LayoutContext &ctx, Size available)
	{
		if (children.empty() || available != lastAvailable)
		{
			lastAvailable = available;
			Element chosen = choose(ctx, available);
			children.clear();
			if (chosen)
			{
				children.push_back(chosen);
				restoreSubtree(ctx, *chosen);
			}
		}
		return children.empty() ? nullptr : children.front().get();
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		auto *child = ensure(ctx, c.biggest());
		return child ? child->measure(ctx, c) : c.clamp({});
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		if (auto *child = ensure(ctx, rect.size()))
			child->arrange(ctx, rect);
	}

  private:
	std::function<Element(const LayoutContext &, Size)> choose;
	Size lastAvailable{-1, -1};
};

class Card : public Wrapper
{
  public:
	Card(Element child, CardOptions options) : Wrapper(std::move(child)), options(options) {}
	const char *name() const override { return "card"; }
	int pad(const LayoutContext &ctx) const
	{
		return options.padding < 0 ? ctx.metrics.padding : options.padding;
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		const auto i = Insets::all(pad(ctx));
		const Size inner = child() ? child()->measure(ctx, c.deflate(i)) : Size{};
		return c.clamp({inner.w + i.horizontal(), inner.h + i.vertical()});
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		if (child())
			child()->arrange(ctx, rect.inset(pad(ctx)));
	}
	void paint(Frame &frame) override
	{
		const auto &palette = frame.layout.theme.palette;
		const int radius = options.radius < 0 ? frame.layout.metrics.radius : options.radius;
		if (options.shadow)
			frame.canvas.fillRounded(bounds.translated(2, 3), radius, palette.shadow);
		frame.canvas.fillRounded(bounds, radius, options.color.value_or(palette.panel));
		if (options.border)
			frame.canvas.strokeRect(bounds, *options.border);
		Wrapper::paint(frame);
	}

  private:
	CardOptions options;
};

class Footer : public Node
{
  public:
	Footer(Element body, Element actions)
	{
		if (!body)
			body = empty();
		if (!actions)
			actions = empty();
		bodyNode = body;
		actionsNode = actions;
		children = {body, actions};
		// Folded, the actions join one page-level scroll so nothing is squeezed to zero.
		stacked = scroll("footer/fold", column({body, actions}));
	}
	const char *name() const override { return "footer"; }
	Node &body() const { return *bodyNode; }
	Node &actions() const { return *actionsNode; }
	bool shouldFold(const LayoutContext &ctx, Constraints c, int &actionsHeight)
	{
		actionsHeight = actions().measure(ctx, c.unboundedH()).h;
		if (!c.boundedH())
			return false;
		const int limit = std::max(ctx.metrics.control, c.maxH / 2);
		return actionsHeight > limit;
	}
	// The tree the host walks (hit tests, focus, clipping) must be the one that
	// was arranged: the fold scroll when folded, the two parts otherwise.
	void setFolded(bool value)
	{
		folded = value;
		if (folded)
			children = {stacked};
		else
			children = {bodyNode, actionsNode};
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		int actionsHeight = 0;
		setFolded(shouldFold(ctx, c, actionsHeight));
		if (folded)
			return stacked->measure(ctx, c);
		const int gap = ctx.metrics.gap;
		Constraints bodyConstraints = c;
		if (c.boundedH())
			bodyConstraints.maxH = std::max(0, c.maxH - actionsHeight - gap);
		bodyConstraints.minH = 0;
		const Size body = this->body().measure(ctx, bodyConstraints);
		const Size actionsSize = actions().measure(ctx, c.unboundedH());
		return c.clamp({std::max(body.w, actionsSize.w), body.h + gap + actionsHeight});
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		int actionsHeight = 0;
		setFolded(shouldFold(ctx, Constraints::loose(rect.size()), actionsHeight));
		if (folded)
		{
			stacked->arrange(ctx, rect);
			return;
		}
		const int gap = ctx.metrics.gap;
		const int bodyHeight = std::max(0, rect.h - actionsHeight - gap);
		body().arrange(ctx, {rect.x, rect.y, rect.w, bodyHeight});
		actions().arrange(ctx, {rect.x, rect.bottom() - actionsHeight, rect.w, actionsHeight});
	}

  private:
	Element bodyNode, actionsNode, stacked;
	bool folded = false;
};

class Field : public Node
{
  public:
	Field(const std::string &labelText, Element control, FieldOptions options)
		: options(options)
	{
		labelNode = paragraph(labelText, {FontRole::Body});
		helpNode = options.help.empty() ? nullptr : paragraph(options.help, {FontRole::Support, true});
		controlNode = control ? control : empty();
		children = {labelNode, controlNode};
		if (helpNode)
			children.push_back(helpNode);
		std::vector<Element> texts{labelNode};
		if (helpNode)
			texts.push_back(helpNode);
		auto captions = column(std::move(texts), {-1, CrossAlign::Stretch});
		stackedLayout = column({captions, controlNode}, {-1, CrossAlign::Stretch});
		inlineLayout = row({expanded(captions), controlNode}, {-1, CrossAlign::Center});
		inlineControlNode = controlNode;
	}
	const char *name() const override { return "field"; }
	Node &layoutFor(const LayoutContext &ctx, int width)
	{
		bool stackedChoice;
		if (options.stacked)
			stackedChoice = *options.stacked;
		else
			stackedChoice = ctx.presentation.compact() || width < ctx.presentation.pt(480);
		if (!stackedChoice)
		{
			// Rebuild the inline row with the control at its column width.
			const int controlWidth = std::min(width / 2, ctx.presentation.pt(options.controlWidth));
			if (controlWidth != lastControlWidth)
			{
				lastControlWidth = controlWidth;
				std::vector<Element> texts{labelNode};
				if (helpNode)
					texts.push_back(helpNode);
				auto captions = column(std::move(texts), {-1, CrossAlign::Stretch});
				inlineLayout = row({expanded(captions), ui::width(controlWidth, controlNode)},
								   {-1, CrossAlign::Center});
			}
		}
		return stackedChoice ? *stackedLayout : *inlineLayout;
	}
	Size measure(const LayoutContext &ctx, Constraints c) override
	{
		return layoutFor(ctx, c.boundedW() ? c.maxW : Constraints::Unbounded).measure(ctx, c);
	}
	void arrange(const LayoutContext &ctx, Rect rect) override
	{
		bounds = rect;
		active = &layoutFor(ctx, rect.w);
		active->arrange(ctx, rect);
	}
	void paint(Frame &frame) override
	{
		if (active)
			active->paint(frame);
	}

  private:
	FieldOptions options;
	Element labelNode, helpNode, controlNode, inlineControlNode, stackedLayout, inlineLayout;
	Node *active = nullptr;
	int lastControlWidth = -1;
};
} // namespace

Element column(std::vector<Element> children, StackOptions options)
{
	return std::make_shared<Axis>(true, std::move(children), options);
}
Element row(std::vector<Element> children, StackOptions options)
{
	return std::make_shared<Axis>(false, std::move(children), options);
}
Element stack(std::vector<Element> children) { return std::make_shared<Overlay>(std::move(children)); }
Element padding(Insets insets, Element child)
{
	return std::make_shared<Padding>(insets, std::move(child));
}
Element pad(int amount, Element child) { return padding(Insets::all(amount), std::move(child)); }
Element padded(Element child) { return std::make_shared<Padding>(Insets{}, std::move(child), true); }
Element align(Alignment alignment, Element child)
{
	return std::make_shared<Align>(alignment, std::move(child));
}
Element center(Element child) { return align(Alignment::Center, std::move(child)); }
Element sized(Size size, Element child)
{
	return std::make_shared<Constrained>(Constraints::tight(size), std::move(child));
}
Element width(int w, Element child)
{
	return std::make_shared<Constrained>(Constraints{w, 0, w, Constraints::Unbounded},
										 std::move(child));
}
Element height(int h, Element child)
{
	return std::make_shared<Constrained>(Constraints{0, h, Constraints::Unbounded, h},
										 std::move(child));
}
Element constrained(Constraints constraints, Element child)
{
	return std::make_shared<Constrained>(constraints, std::move(child));
}
Element maxWidth(int w, Element child)
{
	return std::make_shared<Constrained>(Constraints{0, 0, w, Constraints::Unbounded},
										 std::move(child));
}
Element expanded(Element child, int flex)
{
	auto node = std::make_shared<Wrapper>(std::move(child));
	node->flex = std::max(1, flex);
	return node;
}
Element spacer(int size) { return std::make_shared<Spacer>(size); }
Element expandedSpacer(int flex)
{
	auto node = std::make_shared<Spacer>(0);
	node->flex = std::max(1, flex);
	return node;
}
Element divider() { return std::make_shared<Divider>(); }
Element scroll(const std::string &key, Element child, ScrollOptions options)
{
	return std::make_shared<ScrollArranged>(key, std::move(child), options);
}
Element wrap(std::vector<Element> children, WrapOptions options)
{
	return std::make_shared<Wrap>(std::move(children), options);
}
Element adaptive(std::function<Element(const LayoutContext &, Size)> choose)
{
	return std::make_shared<Adaptive>(std::move(choose));
}
Element card(Element child, CardOptions options)
{
	return std::make_shared<Card>(std::move(child), options);
}
Element footer(Element body, Element actions)
{
	return std::make_shared<Footer>(std::move(body), std::move(actions));
}
Element field(const std::string &label, Element control, FieldOptions options)
{
	return std::make_shared<Field>(label, std::move(control), options);
}
Element form(std::vector<Element> fields)
{
	std::vector<Element> rows;
	for (auto &f : fields)
		if (f)
			rows.push_back(std::move(f));
	return column(std::move(rows), {-1, CrossAlign::Stretch});
}
} // namespace GAGGUI::ui
