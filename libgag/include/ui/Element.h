// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Canvas.h"
#include <SDL3/SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace GAGGUI::ui
{
class Host;
class Node;
using Element = std::shared_ptr<Node>;

class StateStore;
struct LayoutContext
{
	const Presentation &presentation;
	const Theme &theme;
	const Metrics &metrics;
	const TextMeasurer &text;
	// Lets containers that build subtrees during layout restore their state.
	StateStore *states = nullptr;
};

// Framework-owned per-key state that survives rebuilds.
struct NodeState
{
	int scroll = 0;
	std::string text;
	// The model value when the current edit began; a draft returns to it.
	std::string committed;
	std::size_t cursor = 0;
	bool editing = false;
	int highlight = -1;
	int detail = 0;
};

class StateStore
{
  public:
	NodeState &get(const std::string &key) { return states[key]; }
	const NodeState *find(const std::string &key) const
	{
		auto it = states.find(key);
		return it == states.end() ? nullptr : &it->second;
	}
	void erase(const std::string &key) { states.erase(key); }
	void clear() { states.clear(); }

  private:
	std::unordered_map<std::string, NodeState> states;
};

struct KeyEvent
{
	SDL_Keycode sym = SDLK_UNKNOWN;
	Uint16 mod = 0;
	bool repeat = false;
	bool shift() const { return mod & SDL_KMOD_SHIFT; }
	bool ctrl() const { return mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI); }
};

enum class PointerPhase
{
	Down,
	Move,
	Up,
	Cancel
};

// Per-frame interaction snapshot handed to painters.
struct Frame
{
	Canvas &canvas;
	const LayoutContext &layout;
	std::string focusKey, pressedKey, editingKey;
	// Provisional IME text for the editing control.
	std::string composition;
	bool keyboardFocus = false;
	bool hoverValid = false;
	Point hover;
	Uint32 tick = 0;
	bool focused(const std::string &key) const { return keyboardFocus && !key.empty() && key == focusKey; }
	bool pressed(const std::string &key) const { return !key.empty() && key == pressedKey; }
	bool editing(const std::string &key) const { return !key.empty() && key == editingKey; }
	bool hovered(const Rect &bounds) const
	{
		return layout.presentation.hover && hoverValid && bounds.contains(hover);
	}
};

// Base of every element. Builders return shared handles; the host walks the tree.
class Node
{
  public:
	virtual ~Node() = default;
	std::string key;
	// Flex weight inside Row/Column; zero keeps the natural size.
	int flex = 0;
	Rect bounds;
	std::vector<Element> children;

	virtual Size measure(const LayoutContext &context, Constraints constraints) = 0;
	virtual void arrange(const LayoutContext &context, Rect rect) { bounds = rect; }
	// Children outside the current clip are skipped, so long scrolled lists stay cheap.
	virtual void paint(Frame &frame)
	{
		const Rect clip = frame.canvas.clip();
		for (auto &child : children)
			if (child->bounds.intersects(clip))
				child->paint(frame);
	}
	virtual const char *name() const { return "node"; }
	// Text a test or accessibility layer can read for this element.
	virtual std::string accessibleText() const { return {}; }
	virtual std::string tooltipText() const { return {}; }

	// Interaction. Interactive nodes receive taps and keyboard activation.
	virtual bool interactive() const { return false; }
	virtual bool enabled() const { return true; }
	// Addressable parts inside one interactive node (list rows), for diagnostics
	// and harnesses: key suffix, bounds and the text shown.
	struct SubTarget
	{
		std::string suffix;
		Rect bounds;
		std::string label;
	};
	virtual std::vector<SubTarget> subTargets() const { return {}; }
	virtual bool focusable() const { return interactive() && enabled(); }
	// Drags own the pointer from press to release (sliders, scrollbars).
	virtual bool capturesPointer(Point) const { return false; }
	virtual bool scrollable() const { return false; }
	// Keyboard shortcut that activates this node from anywhere on the screen.
	virtual SDL_Keycode shortcut() const { return SDLK_UNKNOWN; }
	virtual void tap(Point, Host &) {}
	// direction: -1/+1 for arrow keys, 0 for Enter/Space.
	virtual void activate(Host &host, int direction);
	virtual void pointer(PointerPhase, Point, Host &) {}
	virtual bool keyDown(const KeyEvent &, Host &) { return false; }
	virtual bool textInput(const std::string &, Host &) { return false; }
	// Editing ended by the host (focus moved, tap elsewhere, Escape when `cancelled`).
	virtual void blur(Host &, bool cancelled) {}
	// Scrolling; `lines` is positive when content should move up.
	virtual void scrollBy(int pixels, Host &) {}
	virtual int scrollOffset() const { return 0; }
	virtual int scrollMaximum() const { return 0; }
	// Absolute scroll position; the node clamps and returns what it applied.
	virtual int scrollTo(int pixels, Host &host)
	{
		scrollBy(pixels - scrollOffset(), host);
		return scrollOffset();
	}
	// Touch drags coast after release and rubber-band past the ends (scroll, list,
	// text). Other scrollables only receive plain scrollBy steps.
	virtual bool inertial() const { return false; }
	// Transient displacement beyond the clamped range while rubber-banding or
	// bouncing; painted, never persisted.
	virtual void setOverscroll(int, Host &) {}
	virtual int overscroll() const { return 0; }
	virtual bool stateful() const { return false; }
	virtual void restore(const NodeState &, const LayoutContext &) {}
	virtual void save(NodeState &) const {}
	// Whether the whole subtree should be clipped to `bounds` when painting/hit testing.
	virtual bool clipsChildren() const { return false; }

	// Innermost hit at `point`, honouring clipping, filtered by `accept`.
	Node *hitTest(Point point, const std::function<bool(const Node &)> &accept);
	void visit(const std::function<void(Node &)> &visitor);
	Node *find(const std::string &key);
};

// Nodes that paint nothing and lay out exactly one child.
class Wrapper : public Node
{
  public:
	explicit Wrapper(Element child)
	{
		if (child)
			children.push_back(std::move(child));
	}
	Node *child() const { return children.empty() ? nullptr : children.front().get(); }
	Size measure(const LayoutContext &context, Constraints constraints) override
	{
		return child() ? child()->measure(context, constraints) : constraints.clamp({});
	}
	void arrange(const LayoutContext &context, Rect rect) override
	{
		bounds = rect;
		if (child())
			child()->arrange(context, rect);
	}
};

// Zero-size placeholder for conditional children.
Element empty();
} // namespace GAGGUI::ui
