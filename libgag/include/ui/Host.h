// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Element.h"
#include <TouchInput.h>
#include <functional>
#include <optional>

namespace GAGGUI::ui
{
struct PopupSpec
{
	Rect anchor;
	std::vector<std::string> options;
	std::vector<bool> enabled;
	int selected = 0;
	std::string help;
	std::function<void(int)> pick;
};

// Owns one element tree: rebuild on invalidation, measure/arrange, hit testing,
// pointer capture, tap-versus-pan, keyboard focus, popups and text editing.
class Host
{
  public:
	using Builder = std::function<Element(const Presentation &)>;
	using Available = std::function<Rect(const Presentation &, const Metrics &)>;
	using Placement = std::function<Rect(Size measured, Rect available)>;

	Host(const Theme &theme, Builder build);
	~Host();

	void setMeasurer(const TextMeasurer *measurer);
	void setPresentation(const Presentation &presentation);
	const Presentation &presentation() const { return current; }
	const Metrics &metrics() const { return metricsValue; }
	const Theme &theme() const { return themeValue; }
	// Where the root may lay out and how its measured size is placed there.
	void setAvailable(Available available) { availableFn = std::move(available); }
	void setPlacement(Placement placement) { placeFn = std::move(placement); }
	void setEscape(std::function<void()> handler) { escape = std::move(handler); }
	// Called after every measure/arrange pass (tree or popup), for diagnostics.
	void setLayoutListener(std::function<void()> listener) { layoutListener = std::move(listener); }
	// Popup items ("popup/<i>") when a choice is open, else null.
	Node *popupRoot() const;

	// Rebuild from the model at the next layout.
	void invalidate();
	// Keep the tree; measure and arrange again.
	void relayout() { needsLayout = true; }
	void layoutIfNeeded();
	bool event(const SDL_Event &event);
	void update(Uint32 tick);
	void paint(Canvas &canvas, Uint32 tick);
	void cancelInput();
	// Drop captures and pans but keep a pending press (window resize, presentation change).
	void cancelGestures();

	// Services for nodes.
	StateStore &states() { return store; }
	NodeState &state(const std::string &key) { return store.get(key); }
	void focus(const std::string &key, bool keyboard = false);
	const std::string &focused() const { return focusKey; }
	bool keyboardFocused() const { return keyboardFocus; }
	void beginEditing(const std::string &key);
	void endEditing(bool cancelled = false);
	const std::string &editing() const { return editingKey; }
	// Provisional IME text for the editing control; never part of its value.
	const std::string &composition() const { return preedit; }
	void openPopup(PopupSpec spec);
	void closePopup();
	bool popupOpen() const { return popup != nullptr; }
	// A press, drag or popup is in progress; screens defer expensive rebuilds meanwhile.
	bool interacting() const { return popup != nullptr || !pressedKey.empty() || !capturedKey.empty(); }
	void scrollIntoView(const std::string &key);

	// Queries, also for harnesses.
	Node *root() const { return tree.get(); }
	Node *find(const std::string &key) const;
	Rect bounds(const std::string &key) const;
	Rect rootBounds() const { return rootRect; }
	std::vector<Node *> interactiveNodes() const;
	std::vector<std::string> focusOrder() const;
	// Synthesize a completed tap at a point (mouse semantics).
	void tapAt(Point point);
	bool debugOverlay = false;

  private:
	const Theme &themeValue;
	Builder build;
	const TextMeasurer *measurer = nullptr;
	Presentation current;
	Metrics metricsValue;
	Available availableFn;
	Placement placeFn;
	std::function<void()> escape, layoutListener;
	Element tree;
	Rect availableRect, rootRect;
	bool dirty = true, needsLayout = true, built = false, rebuilt = false;
	StateStore store;
	std::string focusKey, editingKey, pressedKey, capturedKey, panKey, preedit;
	Node *pressedNode = nullptr;
	bool keyboardFocus = false, hoverValid = false, panning = false;
	Point hover, downPoint;
	GAGCore::TouchInput touch;
	Uint32 lastTick = 0;

	struct Popup
	{
		PopupSpec spec;
		Element tree;
		Rect bounds;
		int highlight = 0;
		std::string scrollKey;
	};
	std::unique_ptr<Popup> popup;
	void layoutPopup();
	Element buildPopup(const PopupSpec &spec);
	bool popupKey(const KeyEvent &key);
	void pickPopup(int index);

	LayoutContext context() const;
	void saveStates();
	void restoreStates();
	void pointer(PointerPhase phase, Point point, std::int64_t device, std::int64_t finger);
	void apply(const std::vector<GAGCore::TouchAction> &actions, Point point);
	Node *interactiveAt(Point point) const;
	Node *scrollableAt(Point point) const;
	bool keyDown(const KeyEvent &key);
	void moveFocus(int direction);
	Node *focusedNode() const;
	void activateShortcut(SDL_Keycode sym);
	Node *activeRoot() const { return popup ? popup->tree.get() : tree.get(); }
};
} // namespace GAGGUI::ui
