// SPDX-License-Identifier: GPL-3.0-or-later
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>
#include <ui/Host.h>
#include <ui/Containers.h>
#include <ui/Controls.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <tuple>

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

namespace
{
std::atomic<std::uint64_t> nextSurface{0};
}
Host::Host(const Theme &theme, Builder build)
	: instance(++nextSurface), themeValue(theme), build(std::move(build))
{
	availableFn = [](const Presentation &p, const Metrics &) { return p.safe; };
	placeFn = [](Size, Rect available) { return available; };
	current = Presentation::forSurface(640, 480);
	metricsValue = resolveMetrics(themeValue, current);
	debugOverlay = SDL_getenv_unsafe("GLOB2_UI_DEBUG") && *SDL_getenv_unsafe("GLOB2_UI_DEBUG") == '1';
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

void Host::setBlockedRegions(std::vector<Rect> regions)
{
	regions.erase(std::remove_if(regions.begin(), regions.end(), [](Rect r) { return r.empty(); }),
				  regions.end());
	std::sort(regions.begin(), regions.end(), [](Rect a, Rect b)
			  { return std::tie(a.x, a.y, a.w, a.h) < std::tie(b.x, b.y, b.w, b.h); });
	regions.erase(std::unique(regions.begin(), regions.end()), regions.end());
	if (regions == blockedRegions)
		return;
	blockedRegions = std::move(regions);
	refreshBlockedGeometry();
}
bool Host::inputBlockedAt(Point point) const
{
	return bool(popup) || std::any_of(blockedRegions.begin(), blockedRegions.end(),
									  [&](Rect region) { return region.contains(point); });
}
bool Host::obscures(Rect visible) const
{
	if (visible.empty())
		return false;
	return std::any_of(blockedRegions.begin(), blockedRegions.end(),
					   [&](Rect region) { return region.intersects(visible); });
}
void Host::refreshBlockedGeometry()
{
	effectiveBounds.clear();
	if (blockedRegions.empty())
		return;
	auto collect = [&](auto &&self, const Node &node, Rect clip) -> void
	{
		if (node.clipsChildren())
			clip = clip.intersect(node.bounds);
		effectiveBounds.emplace(&node, node.bounds.intersect(clip));
		for (const auto &child : node.children)
			if (child)
				self(self, *child, clip);
	};
	if (tree)
		collect(collect, *tree, current.viewport);
	if (popup && popup->tree)
		collect(collect, *popup->tree, current.viewport);
	auto covered = [&](const std::string &key)
	{
		const auto *node = key.empty() ? nullptr : find(key);
		return node && externallyBlocked(*node);
	};
    // pressedNode is an identity token for unkeyed nodes. Never dereference a
    // retained pointer here: a layout rebuild may have replaced its tree.
    const auto pressed=effectiveBounds.find(pressedNode);
	if (covered(capturedKey) || covered(pressedKey) || covered(panKey) ||
        (pressed!=effectiveBounds.end() && obscures(pressed->second)) || (scrolling && covered(scrolling->key)))
		cancelInput();
}
bool Host::externallyBlocked(const Node &node) const
{
	if (blockedRegions.empty())
		return false;
	const auto found = effectiveBounds.find(&node);
	return found != effectiveBounds.end() && obscures(found->second);
}
bool Host::externallyBlockedAt(const Node &node, Point point) const
{
	if (!externallyBlocked(node))
		return false;
	// A list is one interactive node with separately addressable rows. A partly
	// covered row must not disable all of its uncovered siblings.
	const auto found = effectiveBounds.find(&node);
	for (const auto &part : node.subTargets())
		if (part.bounds.contains(point))
			return found == effectiveBounds.end() || obscures(part.bounds.intersect(found->second));
	return true;
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
        pressedNode=nullptr; // retire unkeyed identity before destroying the old tree
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
    if(laidOut)refreshBlockedGeometry();
	validatePopupOwner();
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
				if (node.focusable() && !node.key.empty() && !externallyBlocked(node))
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
	auto *node=focusKey.empty()?nullptr:find(focusKey);
    return node && !externallyBlocked(*node)?node:nullptr;
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
	if (scrolling && gestureScrolling) scrollGestureMotion.cancel(scrolling->axis);
	gestureScrolling = false;
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
	return root->hitTest(point, [&](const Node &n) { return n.interactive() && n.enabled() && !externallyBlockedAt(n,point); });
}

Node *Host::scrollableAt(Point point) const
{
	auto *root = activeRoot();
	if (!root)
		return nullptr;
	return root->hitTest(point, [&](const Node &n) { return n.scrollable() && !externallyBlocked(n); });
}

void Host::apply(const std::vector<GAGCore::TouchAction> &actions, Point point, std::uint64_t device)
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
				axis.setConfig(device == SDL_MOUSE_TOUCHID ? GAGCore::ScrollPresets::mouse() : GAGCore::ScrollPresets::widget());
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

void Host::pointer(PointerPhase phase, Point point, std::uint64_t device, std::uint64_t finger, GAGCore::Ticks time)
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
        if(obscures({point.x,point.y,1,1}))return;
		if (popup && !popup->bounds.contains(point))
		{
			closePopup();
			touch.cancel();
			return;
		}
		auto *root = activeRoot();
		Node *capture = root ? root->hitTest(point, [&](const Node &n) { return n.capturesPointer(point) && !externallyBlockedAt(n,point); }) : nullptr;
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
	pointer(PointerPhase::Down, point, SDL_MOUSE_TOUCHID, 0, lastTick);
	pointer(PointerPhase::Up, point, SDL_MOUSE_TOUCHID, 0, lastTick);
}

namespace
{
std::shared_ptr<SemanticDispatch> commandFor(const SDL_Event &event);
}
bool Host::event(const SDL_Event &event)
{
	layoutIfNeeded();
	if (isSemanticEvent(event))
	{
		auto command = commandFor(event);
		if (!command || command->consumed || command->surface != instance)
			return false;
		command->consumed = true;
		const auto snapshot = inspect();
		auto it = inspected.find(command->key);
		if (it == inspected.end() || it->second.revision != command->revision)
			command->error = "Stale control target; inspect again";
		else
			try
			{
				act(command->key, command->action, command->error);
			}
			catch (const std::exception &error)
			{
				command->error = error.what();
			}
		return true;
	}
	if (touchMouse(event))
		return true;
	const GAGCore::Ticks time = (event.common.timestamp / SDL_NS_PER_MS);
	if (auto sample = GAGCore::scrollGesture(event))
	{
		if (sample->phase == GAGCore::ScrollGesturePhase::Began && sample->sequence != wheelGestureSequence)
		{
			const auto previous = scrolling;
			cancelInput();
			wheelGestureSequence = sample->sequence;
			wheelGestureClaimed = false;
			if (auto *node = scrollableAt({int(sample->x), int(sample->y)}); node && node->inertial())
			{
				const bool resume = previous && previous->key == node->key && previous->lastWritten == node->scrollOffset();
				scrolling = resume ? *previous : ActiveScroll{node->key, GAGCore::ScrollAxis(), node->scrollOffset()};
				scrollGestureMotion.begin(*sample, scrolling->axis,
					resume ? scrolling->axis.offset() : node->scrollOffset(), node->scrollMaximum(), node->bounds.h);
				wheelGestureClaimed = gestureScrolling = true;
			}
		}
		// A tail delivered to a newly opened screen must not acquire its content.
		if (sample->sequence != wheelGestureSequence) return true;
		if (!wheelGestureClaimed) return false;
		if (!scrolling || !gestureScrolling) return true;
		auto *node = find(scrolling->key);
		if (!node || node->scrollOffset() != scrolling->lastWritten) { dropScroll(); return true; }
		scrolling->axis.setBounds(0, node->scrollMaximum(), node->bounds.h);
		scrollGestureMotion.handle(*sample, scrolling->axis);
		writeScroll();
		return true;
	}
	if (gestureScrolling && (event.type == SDL_EVENT_MOUSE_WHEEL || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
		event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_FINGER_DOWN)) dropScroll();
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
				SDL_MOUSE_TOUCHID, 0, time);
		return true;
	case SDL_EVENT_MOUSE_MOTION:
		hover = {int(event.motion.x), int(event.motion.y)};
		hoverValid = true;
		pointer(PointerPhase::Move, hover, SDL_MOUSE_TOUCHID, 0, time);
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
        if(auto *node=find(editingKey);node && externallyBlocked(*node))return false;
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
			if (auto *node = find(editingKey);node && !externallyBlocked(*node))
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
			if (!match && node.enabled() && !externallyBlocked(node) && node.shortcut() == sym)
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
	auto *editing = editingKey.empty() ? nullptr : find(editingKey);
	if (editing && externallyBlocked(*editing) && key.sym == SDLK_TAB)
	{
		// Suspend the stored draft without blur/commit. Activating the field
		// after it is uncovered resumes that draft with its original baseline.
		editing->save(store.get(editingKey));
		editingKey.clear();
		preedit.clear();
		SDL_StopTextInput(SDL_GetKeyboardFocus());
		moveFocus(key.shift() ? -1 : 1);
		return true;
	}
	if (!editingKey.empty() && (!editing || !externallyBlocked(*editing)))
	{
		auto *node = editing;
		if (!preedit.empty() &&
			(key.sym == SDLK_RETURN || key.sym == SDLK_KP_ENTER || key.sym == SDLK_ESCAPE))
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
	if (!focused && ((editing && externallyBlocked(*editing)) ||
					 (!focusKey.empty() && find(focusKey) && externallyBlocked(*find(focusKey)))))
		return true; // suspended UI focus must not trigger background shortcuts
	if (focused && keyboardFocus)
	{
		if (focused->keyDown(key, *this))
			return true;
		if (!key.repeat &&
			(key.sym == SDLK_RETURN || key.sym == SDLK_KP_ENTER || key.sym == SDLK_SPACE))
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
					if (!target && node.scrollable() && node.scrollMaximum() > 0 &&
						!externallyBlocked(node))
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
				if (!match && node.enabled() && !externallyBlocked(node) &&
					node.shortcut() == normalized)
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

void Host::scrollToTop(const std::string &key)
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
		const int delta = target->bounds.y - viewport.y;
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
	layoutIfNeeded();
	if (scrolling && (gestureScrolling || scrolling->axis.isAnimating()))
	{
		auto *node = find(scrolling->key);
		if (!node || node->scrollOffset() != scrolling->lastWritten)
			dropScroll(); // Something else moved the content; it wins.
		else
		{
			scrolling->axis.setBounds(0, node->scrollMaximum(), node->bounds.h);
			if (gestureScrolling) scrollGestureMotion.update(tick, scrolling->axis);
			else scrolling->axis.step(tick);
			writeScroll();
			if (scrolling && !scrolling->axis.isAnimating() && !scrolling->axis.isDragging() &&
				(!gestureScrolling || !scrollGestureMotion.pending()))
			{ scrolling.reset(); gestureScrolling = false; }
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
		if (i < spec.icons.size())
			options.icon = spec.icons[i];
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
	layoutIfNeeded();
	const auto owner =
		spec.ownerKey.empty() ? std::optional<ControlSnapshot>{} : popupOwner(spec.ownerKey);
	if (!spec.ownerKey.empty() && !owner)
		return;
	cancelInput();
	++popupGeneration;
	popup = std::make_unique<Popup>();
	popup->spec = std::move(spec);
	popup->owner = owner;
	popup->highlight =
		std::clamp(popup->spec.selected, 0, std::max(0, int(popup->spec.options.size()) - 1));
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
	const auto &icons = popup->spec.icons;
	const bool hasIcons = std::any_of(icons.begin(), icons.end(), [](const IconRef &icon) { return icon && icon->available(); });
	const int iconExtra = hasIcons ? current.pt(ButtonOptions{}.iconSize) + current.pt(6) : 0;
	for (const auto &option : popup->spec.options)
		width = std::max(width, ctx.text.width(FontRole::Body, option) + iconExtra + metricsValue.padding * 2 + metricsValue.scrollbar);
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
    refreshBlockedGeometry();
	if (layoutListener)
		layoutListener();
}

void Host::closePopup()
{
	if (popup) cancelInput();
	popup.reset();
    refreshBlockedGeometry();
	store.erase("popup/scroll");
	if (layoutListener)
		layoutListener();
}

void Host::pickPopup(int index)
{
	layoutIfNeeded();
	if (!popup)
		return;
	auto pick = popup->spec.pick;
	closePopup();
	if (pick)
		pick(index);
}

std::optional<ControlSnapshot> Host::popupOwner(const std::string &key) const
{
	auto snapshot = inspect();
	std::optional<ControlSnapshot> owner;
	int matches = 0;
	auto findOwner = [&](auto &&self, const ControlSnapshot &item) -> void
	{
		if (item.key == key)
		{
			owner = item;
			++matches;
		}
		for (const auto &child : item.children)
			self(self, child);
	};
	// Search the underlying owner tree, never the popup's own rows.
	if (!snapshot.controls.empty())
		findOwner(findOwner, snapshot.controls.front());
	if (matches != 1 || !owner->enabled || owner->visible.empty() || obscures(owner->visible))
		return {};
	owner->children.clear();
	owner->revision = 0;
	owner->focused = false;
	owner->blocked = false; // Its own popup covers the owner by design.
	return owner;
}

void Host::validatePopupOwner()
{
	if (!popup || popup->spec.ownerKey.empty())
		return;
	const auto owner = popupOwner(popup->spec.ownerKey);
	if (!owner || owner != popup->owner)
		closePopup();
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
            if(auto *target=popup->tree->find("popup/"+std::to_string(popup->highlight));target && !externallyBlocked(*target))
			    pickPopup(popup->highlight);
		return true;
	}
	if (key.sym == SDLK_TAB)
		return true;
	return true;
}

namespace
{
std::mutex semanticMutex;
std::map<std::uint32_t, std::weak_ptr<SemanticDispatch>> semanticCommands;
std::atomic<std::uint32_t> nextCommand{0};
Uint32 semanticEventType()
{
	static const Uint32 type = SDL_RegisterEvents(2) + 1;
	return type;
}
std::shared_ptr<SemanticDispatch> commandFor(const SDL_Event &event)
{
	if (!isSemanticEvent(event))
		return {};
	std::lock_guard lock(semanticMutex);
	auto it = semanticCommands.find(std::uint32_t(event.user.code));
	return it == semanticCommands.end() ? nullptr : it->second.lock();
}
} // namespace
bool isSemanticEvent(const SDL_Event &event)
{
	return event.type == semanticEventType();
}
std::shared_ptr<SemanticDispatch> semanticDispatch()
{
	auto command = std::make_shared<SemanticDispatch>();
	command->id = ++nextCommand;
	std::lock_guard lock(semanticMutex);
	semanticCommands[command->id] = command;
	return command;
}
SemanticDispatch::~SemanticDispatch()
{
	std::lock_guard lock(semanticMutex);
	semanticCommands.erase(id);
}
SDL_Event SemanticDispatch::event()
{
	SDL_Event event{};
	event.type = semanticEventType();
	event.user.code = Sint32(id);
	return event;
}
const char *actionName(ActionKind kind)
{
	switch (kind)
	{
	case ActionKind::Activate:
		return "activate";
	case ActionKind::Select:
		return "select";
	case ActionKind::SetValue:
		return "set_value";
	case ActionKind::Scroll:
		return "scroll";
	}
	return "unknown";
}
bool Node::perform(const SemanticAction &action, Host &host)
{
	if (action.kind == ActionKind::Activate && interactive())
	{
		activate(host, 0);
		return true;
	}
	if (action.kind == ActionKind::Scroll && scrollable())
		if (const auto *pixels = std::get_if<int>(&action.value))
		{
			if (*pixels == 0)
				return true;
			const int offset = scrollOffset();
			const auto next =
				std::clamp(static_cast<long long>(offset) + *pixels,
						   static_cast<long long>(INT_MIN), static_cast<long long>(INT_MAX));
			scrollBy(static_cast<int>(next - offset), host);
			return true;
		}

	return false;
}
SurfaceSnapshot Host::inspect() const
{
	SurfaceSnapshot result;
	result.instance = instance;
	result.viewport = current.viewport;
	result.bounds = rootRect;
	auto collect = [&](auto &&self, const Node &node, Rect clip, bool blocked,
					   bool enabled, const std::string &context) -> ControlSnapshot
	{
		// A node's own clip applies to its parts as well as its children.
		if (node.clipsChildren())
			clip = clip.intersect(node.bounds);
		ControlSnapshot item;
		item.key = node.key;
		item.role = node.name();
		item.label = node.accessibleText();
		item.value = node.controlValue();
		const auto identity = node.semanticIdentity();
		item.actionIdentity = context;
		if (!identity.empty())
			item.actionIdentity += std::to_string(identity.size()) + ":" + identity;
		item.bounds = node.bounds;
		item.visible = node.bounds.intersect(clip);
		item.interactive = node.interactive();
		item.enabled = enabled && node.enabled();
		item.selected = node.selected();
		item.focused = !node.key.empty() && node.key == focusKey;
		item.blocked = blocked || obscures(item.visible);
		item.actions = node.supportedActions();
		for (const auto &part : node.subTargets())
		{
			ControlSnapshot target;
			target.key = node.key + "/" + part.suffix;
			target.role = "option";
			target.label = part.label;
			target.actionIdentity = item.actionIdentity;
			target.bounds = part.bounds;
			target.visible = part.bounds.intersect(clip);
			target.interactive = true;
			target.enabled = item.enabled && part.enabled;
			target.selected = part.selected;
			target.blocked = blocked || obscures(target.visible);
			target.actions = {ActionKind::Activate};
			item.children.push_back(std::move(target));
		}
		for (const auto &child : node.children)
			if (child)
				item.children.push_back(self(self, *child, clip, blocked, item.enabled, item.actionIdentity));
		return item;
	};
	if (tree)
		result.controls.push_back(collect(collect, *tree, current.viewport, bool(popup), true, {}));
	if (popup)
	{
		auto controls = collect(collect, *popup->tree, current.viewport, false, true, {});
		// Reopening an identical-looking popup can bind different callbacks. Its
		// lifetime must participate in every target revision, even when no
		// inspection observed the closed state between the two popups.
		auto identify = [&](auto &&self, ControlSnapshot &item) -> void
		{
			item.actionIdentity = "popup:" + std::to_string(popupGeneration) + ":" + item.actionIdentity;
			for (auto &child : item.children)
				self(self, child);
		};
		identify(identify, controls);
		result.controls.push_back(std::move(controls));
	}
	// Revisions are per target. A counter elsewhere in the HUD never changes a
	// button's revision; removal/recreation does, even with identical content.
	std::map<std::string, ControlSnapshot> next;
	auto revise = [&](auto &&self, std::vector<ControlSnapshot> &items) -> void
	{
		for (auto &item : items)
		{
			if (!item.key.empty())
			{
				auto own = item;
				own.children.clear();
				own.focused = false;
				own.revision = 0;
				auto old = inspected.find(item.key);
				std::uint64_t revision = 0;
				if (old != inspected.end())
				{
					auto before = old->second;
					revision = before.revision;
					before.revision = 0;
					if (before != own)
						revision = 0;
				}
				own.revision = revision ? revision : ++inspectionRevision;
				item.revision = own.revision;
				next.emplace(item.key, std::move(own));
			}
			self(self, item.children);
		}
	};
	revise(revise, result.controls);
	if (result.controls != previousInspection)
		++inspectionRevision;
	previousInspection = result.controls;
	inspected = std::move(next);
	result.revision = inspectionRevision;
	return result;
}
std::optional<ControlSnapshot> Host::actionTarget(const std::string &key, ActionKind kind,
												  std::string &error)
{
	layoutIfNeeded();
	error.clear();
	auto snapshot = inspect();
	ControlSnapshot *target = nullptr;
	int matches = 0;
	auto search = [&](auto &&self, std::vector<ControlSnapshot> &items) -> void
	{
		for (auto &item : items)
		{
			if (item.key == key)
			{
				target = &item;
				++matches;
			}
			self(self, item.children);
		}
	};
	search(search, snapshot.controls);
	if (matches > 1)
		error = "Ambiguous control key; fix duplicate keys in this surface";
	else if (!target)
		error = "Unknown control; inspect the current surface";
	else if (target->blocked)
		error = "Control blocked by a popup or higher input surface";
	else if (!target->enabled)
		error = "Control disabled";
	else if (target->visible.empty())
		error = "Control hidden; scroll its container first";
	else if (std::find(target->actions.begin(), target->actions.end(), kind) ==
			 target->actions.end())
		error = "Action unsupported by control";
	else
	{
		target->children.clear();
		return std::move(*target);
	}
	return {};
}
bool Host::actionAllowed(const std::string &key, ActionKind kind, std::string &error)
{
	return bool(actionTarget(key, kind, error));
}
bool Host::act(const std::string &key, const SemanticAction &action, std::string &error)
{
	auto target = actionTarget(key, action.kind, error);
	if (!target)
		return false;

	{
		// Keep the tree alive: a callback may invalidate or dismiss a popup.
		auto retained = popup ? popup->tree : tree;
		Node *node = retained ? retained->find(key) : nullptr;
		if (node)
		{
			if (action.kind != ActionKind::Scroll && !editingKey.empty() && editingKey != key)
				endEditing();
			if (action.kind != ActionKind::Scroll)
				focus(key);
			if (node->perform(action, *this))
				return true;
			error = "Invalid action value";
		}
		else if (action.kind == ActionKind::Activate)
		{
			// Parts (list rows) share the node's ordinary hit-testing and tap path.
			tapAt(target->visible.center());
			return true;
		}
		else
			error = "Unknown control";
	}
	return false;
}
std::string inspectionJSON(const SurfaceSnapshot &snapshot)
{
	using Json = nlohmann::json;
	auto rect = [](Rect r) { return Json{{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}}; };
	auto encode = [&](auto &&self, const ControlSnapshot &item) -> Json
	{
		Json actions = Json::array(), children = Json::array(), value;
		for (auto action : item.actions)
			actions.push_back(actionName(action));
		for (const auto &child : item.children)
			children.push_back(self(self, child));
		std::visit(
			[&](const auto &v)
			{
				using T = std::decay_t<decltype(v)>;
				if constexpr (!std::is_same_v<T, std::monostate>)
					value = v;
			},
			item.value);
		return {{"key", item.key},
				{"revision", item.revision},
				{"role", item.role},
				{"label", item.label},
				{"value", value},
				{"bounds", rect(item.bounds)},
				{"visible", rect(item.visible)},
				{"enabled", item.enabled},
				{"selected", item.selected},
				{"focused", item.focused},
				{"blocked", item.blocked},
				{"actions", actions},
				{"children", children}};
	};
	Json controls = Json::array();
	for (const auto &item : snapshot.controls)
		controls.push_back(encode(encode, item));
	return Json{{"instance", snapshot.instance},
				{"revision", snapshot.revision},
				{"viewport", rect(snapshot.viewport)},
				{"bounds", rect(snapshot.bounds)},
				{"controls", controls}}
		.dump(-1, ' ', false, Json::error_handler_t::replace);
}
std::string diagnosticJSON(const SurfaceSnapshot &snapshot)
{
	using Json = nlohmann::json;
	// Legacy browser diagnostics flattened control characters to spaces. Keep
	// that contract here while typed snapshots retain the actual text/value.
	auto legacyText = [](std::string value)
	{
		for (auto &c : value)
			if (static_cast<unsigned char>(c) < 0x20)
				c = ' ';
		return value;
	};
	auto rect = [](Rect r) { return Json{{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}}; };
	Json output = {{"surface", {{"w", snapshot.viewport.w}, {"h", snapshot.viewport.h}}},
				   {"root", rect(snapshot.bounds)},
				   {"controls", Json::object()}};
	auto append = [&](auto &&self, const std::vector<ControlSnapshot> &items) -> void
	{
		for (const auto &item : items)
		{
			if (!item.key.empty() && item.interactive)
			{
				auto value = rect(item.bounds);
				value["visible"] = rect(item.visible);
				value["enabled"] = item.enabled;
				value["label"] = legacyText(item.label);
				output["controls"][legacyText(item.key)] = std::move(value);
			}
			self(self, item.children);
		}
	};
	append(append, snapshot.controls);
	return output.dump(-1, ' ', false, Json::error_handler_t::replace);
}
} // namespace GAGGUI::ui
