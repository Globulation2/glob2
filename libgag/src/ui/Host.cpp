// SPDX-License-Identifier: GPL-3.0-or-later
#include <ui/Host.h>
#include <ui/Containers.h>
#include <ui/Controls.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace GAGGUI::ui
{
namespace
{
const FixedTextMeasurer fallbackMeasurer;
bool touchMouse(const SDL_Event &event)
{
	return (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID) ||
		   ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
			event.button.which == SDL_TOUCH_MOUSEID);
}
} // namespace

Host::Host(const Theme &theme, Builder build) : themeValue(theme), build(std::move(build))
{
	availableFn = [](const Presentation &p, const Metrics &) { return p.safe; };
	placeFn = [](Size, Rect available) { return available; };
	current = Presentation::forSurface(640, 480);
	metricsValue = resolveMetrics(themeValue, current);
	debugOverlay = SDL_getenv("GLOB2_UI_DEBUG") && *SDL_getenv("GLOB2_UI_DEBUG") == '1';
}

Host::~Host()
{
	if (!editingKey.empty())
		SDL_StopTextInput(SDL_GetKeyboardFocus());
}

void Host::setMeasurer(const TextMeasurer *value)
{
	measurer = value;
	needsLayout = true;
}

void Host::setPresentation(const Presentation &presentation)
{
	if (presentation == current && built)
		return;
	current = presentation;
	metricsValue = resolveMetrics(themeValue, current);
	dirty = true;
	needsLayout = true;
	cancelGestures();
}

LayoutContext Host::context() const
{
	return {current, themeValue, metricsValue, measurer ? *measurer : fallbackMeasurer,
			const_cast<StateStore *>(&store)};
}

void Host::invalidate()
{
	dirty = true;
	needsLayout = true;
}

void Host::saveStates()
{
	if (!tree)
		return;
	tree->visit(
		[&](Node &node)
		{
			if (node.stateful() && !node.key.empty())
				node.save(store.get(node.key));
		});
}

void Host::restoreStates()
{
	if (!tree)
		return;
	const auto ctx = context();
	std::vector<std::string> seen;
	tree->visit(
		[&](Node &node)
		{
			if (node.key.empty())
				return;
			if (std::find(seen.begin(), seen.end(), node.key) != seen.end())
				std::fprintf(stderr, "ui: duplicate element key '%s'\n", node.key.c_str());
			seen.push_back(node.key);
			if (node.stateful())
				if (const auto *state = store.find(node.key))
					node.restore(*state, ctx);
		});
}

void Host::layoutIfNeeded()
{
	bool laidOut = false;
	if (dirty)
	{
		saveStates();
		tree = build(current);
		if (!tree)
			tree = empty();
		restoreStates();
		dirty = false;
		built = true;
		needsLayout = true;
		rebuilt = true;
		pressedNode = nullptr;
	}
	if (needsLayout && tree)
	{
		laidOut = true;
		const auto ctx = context();
		availableRect = availableFn(current, metricsValue);
		const Size measured = tree->measure(ctx, Constraints::loose(availableRect.size()));
		rootRect = placeFn(measured, availableRect);
		tree->arrange(ctx, rootRect);
		needsLayout = false;
		// Adaptive containers choose their subtree during layout, so the tree is
		// only complete here: bind text controls (idempotent per node) and drop
		// focus, editing and capture whose element no longer exists.
		bindTextControls(*this, *tree);
		if (rebuilt)
		{
			rebuilt = false;
			if (!focusKey.empty() && !tree->find(focusKey))
			{
				focusKey.clear();
				keyboardFocus = false;
			}
			if (!editingKey.empty() && !tree->find(editingKey))
				endEditing();
			if (!capturedKey.empty() && !tree->find(capturedKey))
				capturedKey.clear();
		}
		if (popup)
			layoutPopup();
	}
	if (laidOut && layoutListener)
		layoutListener();
}

Node *Host::popupRoot() const { return popup && popup->tree ? popup->tree.get() : nullptr; }

Node *Host::find(const std::string &key) const
{
	if (popup && popup->tree)
		if (auto *node = popup->tree->find(key))
			return node;
	return tree ? tree->find(key) : nullptr;
}

Rect Host::bounds(const std::string &key) const
{
	auto *node = find(key);
	if (!node)
		throw std::runtime_error("ui: no element with key '" + key + "'");
	return node->bounds;
}

std::vector<Node *> Host::interactiveNodes() const
{
	std::vector<Node *> out;
	if (!tree)
		return out;
	tree->visit(
		[&](Node &node)
		{
			if (node.interactive())
				out.push_back(&node);
		});
	return out;
}

std::vector<std::string> Host::focusOrder() const
{
	std::vector<std::string> out;
	if (auto *root = activeRoot())
		root->visit(
			[&](Node &node)
			{
				if (node.focusable() && !node.key.empty())
					out.push_back(node.key);
			});
	return out;
}

void Host::focus(const std::string &key, bool keyboard)
{
	focusKey = key;
	keyboardFocus = keyboard && !key.empty();
}

Node *Host::focusedNode() const
{
	return focusKey.empty() ? nullptr : find(focusKey);
}

void Host::beginEditing(const std::string &key)
{
	if (editingKey == key)
		return;
	if (auto *previous = editingKey.empty() ? nullptr : find(editingKey))
		previous->save(store.get(editingKey));
	editingKey = key;
	focusKey = key;
	keyboardFocus = false;
	if (!key.empty())
		SDL_StartTextInput(SDL_GetKeyboardFocus());
	else
		SDL_StopTextInput(SDL_GetKeyboardFocus());
	needsLayout = true;
}

void Host::endEditing(bool cancelled)
{
	preedit.clear();
	if (editingKey.empty())
		return;
	const std::string key = editingKey;
	editingKey.clear();
	SDL_StopTextInput(SDL_GetKeyboardFocus());
	needsLayout = true;
	if (auto *node = find(key))
		node->blur(*this, cancelled);
}

void Host::cancelInput()
{
	touch.cancel();
	cancelGestures();
	pressedKey.clear();
	pressedNode = nullptr;
}

void Host::cancelGestures()
{
	// Captures and pans cannot continue across a layout change. A plain press
	// stays pending: its release re-hit-tests and only lands on the same key,
	// so a click whose release is already queued when the window resizes is
	// not lost.
	if (!capturedKey.empty())
		if (auto *node = find(capturedKey))
			node->pointer(PointerPhase::Cancel, downPoint, *this);
	capturedKey.clear();
	panKey.clear();
	panning = false;
	dropScroll();
}

void Host::writeScroll()
{
	if (!scrolling)
		return;
	auto *node = find(scrolling->key);
	if (!node)
	{
		scrolling.reset();
		return;
	}
	const auto &axis = scrolling->axis;
	node->scrollTo(int(std::lround(axis.clampedOffset())), *this);
	node->setOverscroll(int(std::lround(axis.overscroll())), *this);
	scrolling->lastWritten = node->scrollOffset();
}

void Host::dropScroll()
{
	if (!scrolling)
		return;
	if (auto *node = find(scrolling->key))
		node->setOverscroll(0, *this);
	scrolling.reset();
}

// The finger lifted without dragging: stretched content springs back, settled
// content releases the axis.
void Host::settleScroll(GAGCore::Ticks time)
{
	if (!scrolling || scrolling->axis.isDragging())
		return;
	if (!scrolling->axis.isAnimating())
		scrolling->axis.settle(time);
	if (!scrolling->axis.isAnimating())
	{
		writeScroll();
		scrolling.reset();
	}
}

Node *Host::interactiveAt(Point point) const
{
	auto *root = activeRoot();
	if (!root)
		return nullptr;
	if (popup && !popup->bounds.contains(point))
		return nullptr;
	return root->hitTest(point, [](const Node &n) { return n.interactive() && n.enabled(); });
}

Node *Host::scrollableAt(Point point) const
{
	auto *root = activeRoot();
	if (!root)
		return nullptr;
	return root->hitTest(point, [](const Node &n) { return n.scrollable(); });
}

void Host::apply(const std::vector<GAGCore::TouchAction> &actions, Point point, std::int64_t device)
{
	for (const auto &action : actions)
	{
		if (action.kind == GAGCore::TouchActionKind::Pan)
		{
			if (!panning)
			{
				panning = true;
				pressedKey.clear();
				pressedNode = nullptr;
			}
			if (panKey.empty())
				continue;
			auto *node = find(panKey);
			if (!node)
				continue;
			if (!node->inertial())
			{
				node->scrollBy(-int(std::lround(action.point.y)), *this);
				continue;
			}
			if (!scrolling || scrolling->key != panKey)
				scrolling = ActiveScroll{panKey, GAGCore::ScrollAxis(), 0};
			auto &axis = scrolling->axis;
			if (!axis.isDragging())
			{
				// A mouse drag follows the pointer and stops with it; a finger
				// gets momentum and bounce. Content stopped mid-bounce by this
				// touch keeps its stretch and continues from there.
				axis.setConfig(device == -1 ? GAGCore::ScrollPresets::mouse() : GAGCore::ScrollPresets::widget());
				axis.setBounds(0, node->scrollMaximum(), node->bounds.h);
				if (axis.overscroll() == 0)
					axis.setOffset(node->scrollOffset());
				axis.beginDrag(action.time);
			}
			axis.drag(action.time, -action.point.y);
			writeScroll();
		}
		else if (action.kind == GAGCore::TouchActionKind::PanEnd)
		{
			if (scrolling && scrolling->axis.isDragging())
			{
				scrolling->axis.endDrag(action.time);
				writeScroll();
				if (scrolling && !scrolling->axis.isAnimating())
					scrolling.reset();
			}
		}
		else if (action.kind == GAGCore::TouchActionKind::Select && swallowTap)
		{
			swallowTap = false;
			pressedKey.clear();
			pressedNode = nullptr;
		}
		else if (action.kind == GAGCore::TouchActionKind::Select)
		{
			auto *node = interactiveAt(point);
			const bool same = node && (node->key.empty() ? node == pressedNode : node->key == pressedKey);
			if (popup && !popup->bounds.contains(point))
				closePopup();
			else if (same)
			{
				if (!editingKey.empty() && node->key != editingKey)
					endEditing();
				focus(node->key, false);
				node->tap(point, *this);
			}
			else if (!node && !editingKey.empty())
				endEditing();
			pressedKey.clear();
			pressedNode = nullptr;
		}
		else if (action.kind == GAGCore::TouchActionKind::Cancel)
		{
			pressedKey.clear();
			pressedNode = nullptr;
			dropScroll();
		}
	}
}

void Host::pointer(PointerPhase phase, Point point, std::int64_t device, std::int64_t finger, GAGCore::Ticks time)
{
	layoutIfNeeded();
	if (phase == PointerPhase::Down)
	{
		downPoint = point;
		panning = false;
		swallowTap = false;
		if (scrolling && scrolling->axis.isAnimating())
		{
			// A touch on coasting content stops it where it is and is not a tap.
			scrolling->axis.interrupt();
			writeScroll();
			auto *under = scrollableAt(point);
			swallowTap = scrolling && under && under->key == scrolling->key;
		}
		if (popup && !popup->bounds.contains(point))
		{
			closePopup();
			touch.cancel();
			return;
		}
		auto *root = activeRoot();
		Node *capture = root ? root->hitTest(point, [&](const Node &n) { return n.capturesPointer(point); }) : nullptr;
		if (capture && !capture->key.empty())
		{
			capturedKey = capture->key;
			focus(capture->key, false);
			capture->pointer(PointerPhase::Down, point, *this);
			return;
		}
		if (auto *node = interactiveAt(point))
		{
			pressedKey = node->key;
			pressedNode = node;
		}
		auto *scrollNode = scrollableAt(point);
		panKey = scrollNode ? scrollNode->key : "";
		apply(touch.down(device, finger, {double(point.x), double(point.y)}, time), point, device);
		return;
	}
	if (!capturedKey.empty())
	{
		auto *node = find(capturedKey);
		if (phase == PointerPhase::Up || phase == PointerPhase::Cancel)
		{
			if (node)
				node->pointer(phase, point, *this);
			capturedKey.clear();
		}
		else if (node)
			node->pointer(PointerPhase::Move, point, *this);
		return;
	}
	if (phase == PointerPhase::Move)
		apply(touch.move(device, finger, {double(point.x), double(point.y)}, time), point, device);
	else if (phase == PointerPhase::Up)
	{
		apply(touch.up(device, finger, {double(point.x), double(point.y)}, time), point, device);
		settleScroll(time);
	}
	else
		apply(touch.cancel(), point, device);
}

void Host::tapAt(Point point)
{
	pointer(PointerPhase::Down, point, -1, 0, lastTick);
	pointer(PointerPhase::Up, point, -1, 0, lastTick);
}

bool Host::event(const SDL_Event &event)
{
	layoutIfNeeded();
	if (touchMouse(event))
		return true;
	const GAGCore::Ticks time = (event.common.timestamp / SDL_NS_PER_MS);
	switch (event.type)
	{
	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_MOTION:
	case SDL_EVENT_FINGER_UP:
	{
		const Point point{int(event.tfinger.x * current.viewport.w),
						  int(event.tfinger.y * current.viewport.h)};
		const auto phase = event.type == SDL_EVENT_FINGER_DOWN   ? PointerPhase::Down
						   : event.type == SDL_EVENT_FINGER_UP ? PointerPhase::Up
														  : PointerPhase::Move;
		pointer(phase, point, event.tfinger.touchID, event.tfinger.fingerID, time);
		return true;
	}
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event.button.button != SDL_BUTTON_LEFT)
			return false;
		hover = {int(event.button.x), int(event.button.y)};
		hoverValid = true;
		pointer(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PointerPhase::Down : PointerPhase::Up, hover,
				-1, 0, time);
		return true;
	case SDL_EVENT_MOUSE_MOTION:
		hover = {int(event.motion.x), int(event.motion.y)};
		hoverValid = true;
		pointer(PointerPhase::Move, hover, -1, 0, time);
		return true;
	case SDL_EVENT_MOUSE_WHEEL:
	{
		if (!hoverValid)
			return false;
		const int direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
		const float lines = event.wheel.y * direction;
		if (lines == 0)
			return false;
		cancelInput();
		if (auto *node = scrollableAt(hover))
		{
			node->scrollBy(-lines * metricsValue.control, *this);
			return true;
		}
		return false;
	}
	case SDL_EVENT_KEY_DOWN:
		return keyDown({event.key.key, event.key.mod, event.key.repeat != 0});
	case SDL_EVENT_TEXT_EDITING:
		// Provisional IME text is shown, never committed; Return while composing
		// picks a candidate and must not submit the control.
		if (editingKey.empty())
			return false;
		preedit = event.edit.text;
		needsLayout = true;
		return true;
	case SDL_EVENT_TEXT_INPUT:
		preedit.clear();
		if (!editingKey.empty())
			if (auto *node = find(editingKey))
				return node->textInput(event.text.text, *this);
		return false;
	case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
		if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
			cancelInput();
		else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
			cancelGestures();
		return false;
	case SDL_EVENT_WILL_ENTER_BACKGROUND:
		cancelInput();
		return false;
	default:
		return false;
	}
}

void Host::activateShortcut(SDL_Keycode sym)
{
	if (!tree)
		return;
	Node *match = nullptr;
	tree->visit(
		[&](Node &node)
		{
			if (!match && node.enabled() && node.shortcut() == sym)
				match = &node;
		});
	if (match)
		match->activate(*this, 0);
}

void Host::moveFocus(int direction)
{
	const auto order = focusOrder();
	if (order.empty())
		return;
	int index = -1;
	for (std::size_t i = 0; i < order.size(); ++i)
		if (order[i] == focusKey)
			index = int(i);
	if (index < 0)
		index = direction > 0 ? 0 : int(order.size()) - 1;
	else
		index = (index + direction + int(order.size())) % int(order.size());
	focus(order[index], true);
	scrollIntoView(order[index]);
}

bool Host::keyDown(const KeyEvent &key)
{
	if (popup)
		return popupKey(key);
	if (!editingKey.empty())
	{
		auto *node = find(editingKey);
		if (!preedit.empty() && (key.sym == SDLK_RETURN || key.sym == SDLK_KP_ENTER || key.sym == SDLK_ESCAPE))
		{
			if (key.sym == SDLK_ESCAPE)
				preedit.clear();
			return true;
		}
		if (key.sym == SDLK_TAB)
		{
			endEditing();
			moveFocus(key.shift() ? -1 : 1);
			return true;
		}
		if (node && node->keyDown(key, *this))
			return true;
		if (key.sym != SDLK_ESCAPE)
			return false;
		// Escape leaves the field and still reaches the panel behind it, so a
		// dialog closes on the first press as it always did.
		endEditing(true);
	}
	if (key.sym == SDLK_TAB)
	{
		moveFocus(key.shift() ? -1 : 1);
		return true;
	}
	auto *focused = focusedNode();
	if (focused && keyboardFocus)
	{
		if (focused->keyDown(key, *this))
			return true;
		if (!key.repeat && (key.sym == SDLK_RETURN || key.sym == SDLK_KP_ENTER || key.sym == SDLK_SPACE))
		{
			focused->activate(*this, 0);
			return true;
		}
		if (key.sym == SDLK_LEFT || key.sym == SDLK_RIGHT)
		{
			focused->activate(*this, key.sym == SDLK_LEFT ? -1 : 1);
			return true;
		}
		if (key.sym == SDLK_UP || key.sym == SDLK_DOWN)
		{
			moveFocus(key.sym == SDLK_UP ? -1 : 1);
			return true;
		}
	}
	else if (focused && focused->keyDown(key, *this))
		return true;
	if (key.repeat)
		return false;
	if (key.sym == SDLK_PAGEUP || key.sym == SDLK_PAGEDOWN)
	{
		Node *target = focused;
		if (!target && hoverValid)
			target = scrollableAt(hover);
		if (!target && tree)
			tree->visit(
				[&](Node &node)
				{
					if (!target && node.scrollable() && node.scrollMaximum() > 0)
						target = &node;
				});
		if (target && !target->scrollable())
			target = nullptr;
		if (target)
		{
			target->scrollBy((key.sym == SDLK_PAGEUP ? -1 : 1) * target->bounds.h, *this);
			return true;
		}
	}
	const SDL_Keycode normalized = key.sym == SDLK_KP_ENTER ? SDLK_RETURN : key.sym;
	Node *match = nullptr;
	if (tree)
		tree->visit(
			[&](Node &node)
			{
				if (!match && node.enabled() && node.shortcut() == normalized)
					match = &node;
			});
	if (match)
	{
		match->activate(*this, 0);
		return true;
	}
	if (normalized == SDLK_ESCAPE && escape)
	{
		escape();
		return true;
	}
	return false;
}

void Host::scrollIntoView(const std::string &key)
{
	layoutIfNeeded();
	auto *root = activeRoot();
	if (!root)
		return;
	std::vector<Node *> path;
	std::function<bool(Node &)> search = [&](Node &node)
	{
		path.push_back(&node);
		if (!key.empty() && node.key == key)
			return true;
		for (auto &child : node.children)
			if (search(*child))
				return true;
		path.pop_back();
		return false;
	};
	if (!search(*root))
		return;
	Node *target = path.back();
	for (int i = int(path.size()) - 2; i >= 0; --i)
	{
		auto *ancestor = path[std::size_t(i)];
		if (!ancestor->scrollable())
			continue;
		const Rect viewport = ancestor->bounds;
		int delta = 0;
		if (target->bounds.y < viewport.y)
			delta = target->bounds.y - viewport.y;
		else if (target->bounds.bottom() > viewport.bottom())
			delta = std::min(target->bounds.bottom() - viewport.bottom(),
							 target->bounds.y - viewport.y);
		if (delta)
		{
			ancestor->scrollBy(delta, *this);
			layoutIfNeeded();
		}
	}
}

void Host::update(Uint32 tick)
{
	lastTick = tick;
	if (scrolling && scrolling->axis.isAnimating())
	{
		auto *node = find(scrolling->key);
		if (!node || node->scrollOffset() != scrolling->lastWritten)
			dropScroll(); // Something else moved the content; it wins.
		else
		{
			scrolling->axis.setBounds(0, node->scrollMaximum(), node->bounds.h);
			scrolling->axis.step(tick);
			writeScroll();
			if (scrolling && !scrolling->axis.isAnimating())
				scrolling.reset();
		}
	}
	layoutIfNeeded();
}

void Host::dismissTooltip(const std::string &key)
{
	tooltipSuppressedKey = key;
	tooltipKey.clear();
}

void Host::paint(Canvas &canvas, Uint32 tick)
{
	layoutIfNeeded();
	if (!tree)
		return;
	const auto ctx = context();
	Frame frame{canvas, ctx};
	frame.focusKey = focusKey;
	frame.pressedKey = pressedKey;
	frame.editingKey = editingKey;
	frame.composition = preedit;
	frame.keyboardFocus = keyboardFocus;
	frame.hoverValid = hoverValid && !popup;
	frame.hover = hover;
	frame.tick = tick;
	tree->paint(frame);
	if (popup && popup->tree)
	{
		const auto &palette = themeValue.palette;
		canvas.fillRounded(popup->bounds.translated(2, 3), metricsValue.radius, palette.shadow.applyAlpha(100));
		canvas.fillRounded(popup->bounds, metricsValue.radius, palette.panel);
		canvas.strokeRect(popup->bounds, palette.muted);
		Frame popupFrame = frame;
		popupFrame.hoverValid = hoverValid;
		popupFrame.focusKey = "popup/" + std::to_string(popup->highlight);
		popupFrame.keyboardFocus = true;
		popup->tree->paint(popupFrame);
	}
	if (keyboardFocus && !focusKey.empty() && !popup)
		if (auto *node = find(focusKey))
			canvas.strokeRect(node->bounds.inset(-metricsValue.focusRing + 1), themeValue.palette.focus);
	Node *tip = nullptr;
	if (!popup && pressedKey.empty())
	{
		if (keyboardFocus)
			tip = focusedNode();
		else if (current.hover && hoverValid)
			tip = interactiveAt(hover);
	}
	const std::string candidate =
		tip && tip->enabled() && !tip->tooltipText().empty() ? tip->key : "";
	if (candidate != tooltipSuppressedKey)
		tooltipSuppressedKey.clear();
	if (candidate != tooltipKey)
	{
		tooltipKey = candidate;
		tooltipSince = tick;
	}
	if (!candidate.empty() && candidate != tooltipSuppressedKey &&
		Uint32(tick - tooltipSince) >= 600)
	{
		const auto &p = themeValue.palette;
		const int pad = current.pt(8);
		const int maxWidth = std::max(1, std::min(current.pt(280), current.safe.w) - 2 * pad);
		const auto text = layoutText(canvas.measurer(), FontRole::Support, tip->tooltipText(),
									 maxWidth, metricsValue.lineGap);
		int width = 0;
		for (const auto &line : text.lines)
			width = std::max(width, canvas.measurer().width(FontRole::Support, line));
		const int w = std::min(current.safe.w, width + 2 * pad),
				  h = std::min(current.safe.h, text.height + 2 * pad);
		const int x = std::clamp(tip->bounds.x, current.safe.x, current.safe.right() - w);
		const int proposedY = tip->bounds.bottom() + current.pt(4);
		const int y = std::clamp(
			proposedY + h <= current.safe.bottom() ? proposedY : tip->bounds.y - h - current.pt(4),
			current.safe.y, current.safe.bottom() - h);
		canvas.pushClip(current.safe);
		canvas.fillRounded({x, y, w, h}, metricsValue.radius, p.panel);
		canvas.strokeRect({x, y, w, h}, p.line);
		int lineY = y + pad;
		for (const auto &line : text.lines)
		{
			canvas.text({x + pad, lineY}, FontRole::Support, line, p.ink);
			lineY += canvas.measurer().lineHeight(FontRole::Support) + metricsValue.lineGap;
		}
		canvas.popClip();
	}
	if (debugOverlay)
		tree->visit(
			[&](Node &node)
			{
				if (!node.interactive() && !node.scrollable())
					return;
				canvas.strokeRect(node.bounds, GAGCore::Color(255, 0, 255));
				canvas.text({node.bounds.x + 2, node.bounds.y + 2}, FontRole::Caption, node.key,
							GAGCore::Color(255, 0, 255));
			});
}

// Popups are ordinary element trees built from the same controls.
Element Host::buildPopup(const PopupSpec &spec)
{
	std::vector<Element> rows;
	for (std::size_t i = 0; i < spec.options.size(); ++i)
	{
		const bool ok = spec.enabled.empty() || spec.enabled[i];
		ButtonOptions options;
		options.enabled = ok;
		options.selected = int(i) == spec.selected;
		options.flat = true;
		options.alignLeft = true;
		rows.push_back(button("popup/" + std::to_string(i), spec.options[i],
							  [this, i] { pickPopup(int(i)); }, options));
	}
	std::vector<Element> parts;
	parts.push_back(scroll("popup/scroll", column(std::move(rows), {metricsValue.halfGap / 2})));
	if (!spec.help.empty())
		parts.push_back(paragraph(spec.help, {FontRole::Support, true}));
	return padding(Insets::all(metricsValue.halfGap), column(std::move(parts)));
}

void Host::openPopup(PopupSpec spec)
{
	popup = std::make_unique<Popup>();
	popup->spec = std::move(spec);
	popup->highlight = std::clamp(popup->spec.selected, 0, std::max(0, int(popup->spec.options.size()) - 1));
	popup->tree = buildPopup(popup->spec);
	store.erase("popup/scroll");
	layoutPopup();
	scrollIntoView("popup/" + std::to_string(popup->highlight));
}

void Host::layoutPopup()
{
	if (!popup || !popup->tree)
		return;
	const auto ctx = context();
	const Rect safe = availableRect.empty() ? current.dialog : current.dialog.intersect(availableRect).empty() ? current.dialog : current.dialog;
	const int margin = metricsValue.padding;
	const auto &anchor = popup->spec.anchor;
	int width = std::max(anchor.w, popup->spec.help.empty() ? 0 : current.pt(330));
	for (const auto &option : popup->spec.options)
		width = std::max(width, ctx.text.width(FontRole::Body, option) + metricsValue.padding * 2 + metricsValue.scrollbar);
	width = std::max(1, std::min(width, safe.w - 2 * margin));
	const int maxHeight = std::max(metricsValue.control, safe.h - 2 * margin);
	const Size measured = popup->tree->measure(ctx, {width, 0, width, maxHeight});
	int x = std::clamp(anchor.x, safe.x + margin, std::max(safe.x + margin, safe.right() - width - margin));
	int y = anchor.bottom() + metricsValue.halfGap;
	if (y + measured.h > safe.bottom() - margin)
		y = anchor.y - measured.h - metricsValue.halfGap;
	y = std::clamp(y, safe.y + margin, std::max(safe.y + margin, safe.bottom() - measured.h - margin));
	popup->bounds = {x, y, width, measured.h};
	popup->tree->arrange(ctx, popup->bounds);
	if (layoutListener)
		layoutListener();
}

void Host::closePopup()
{
	popup.reset();
	store.erase("popup/scroll");
	if (layoutListener)
		layoutListener();
}

void Host::pickPopup(int index)
{
	if (!popup)
		return;
	auto pick = popup->spec.pick;
	closePopup();
	if (pick)
		pick(index);
}

bool Host::popupKey(const KeyEvent &key)
{
	if (!popup)
		return false;
	const int count = int(popup->spec.options.size());
	if (key.sym == SDLK_ESCAPE)
	{
		closePopup();
		return true;
	}
	if (key.sym == SDLK_UP || key.sym == SDLK_DOWN || key.sym == SDLK_HOME || key.sym == SDLK_END ||
		key.sym == SDLK_PAGEUP || key.sym == SDLK_PAGEDOWN)
	{
		if (count)
		{
			int next = popup->highlight;
			if (key.sym == SDLK_UP)
				next -= 1;
			else if (key.sym == SDLK_DOWN)
				next += 1;
			else if (key.sym == SDLK_HOME)
				next = 0;
			else if (key.sym == SDLK_END)
				next = count - 1;
			else
				next += key.sym == SDLK_PAGEUP ? -8 : 8;
			popup->highlight = std::clamp(next, 0, count - 1);
			scrollIntoView("popup/" + std::to_string(popup->highlight));
		}
		return true;
	}
	if (key.sym == SDLK_RETURN || key.sym == SDLK_KP_ENTER || key.sym == SDLK_SPACE)
	{
		if (count && (popup->spec.enabled.empty() || popup->spec.enabled[popup->highlight]))
			pickPopup(popup->highlight);
		return true;
	}
	if (key.sym == SDLK_TAB)
		return true;
	return true;
}
} // namespace GAGGUI::ui
