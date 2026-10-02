// SPDX-License-Identifier: GPL-3.0-or-later
// Layout-engine checks without fonts or a window: fixed-advance text, recording canvas.
#include "Glob2Test.h"
#include <string>
#include <stdexcept>
#include <functional>
#include <utility>
#include <ui/Containers.h>
#include <ui/Controls.h>
#include <ui/Host.h>
#include "UIRecordingCanvas.h"
#include <BrowserTextInput.h>
#include <cstdio>
#include <vector>

using namespace GAGGUI::ui;
using glob2test::RecordingCanvas;

namespace
{
void require(bool condition, const char *message)
{
	GLOB2_REQUIRE(condition, message);
}

Theme theme;
FixedTextMeasurer measurer(8, 16);

struct Fixture
{
	Host host;
	Presentation presentation;
	// Event timestamps and Host::update ticks come from this clock only.
	Uint32 clock = 1000;
	Fixture(Host::Builder build, int width, int height, bool touch = false)
		: host(theme, std::move(build)), presentation(Presentation::forSurface(width, height, 1, touch))
	{
		host.setMeasurer(&measurer);
		host.setPresentation(presentation);
		host.layoutIfNeeded();
	}
	void resize(int width, int height)
	{
		presentation = Presentation::forSurface(width, height, 1, presentation.touch);
		host.setPresentation(presentation);
		host.layoutIfNeeded();
	}
	RecordingCanvas paint()
	{
		RecordingCanvas canvas(presentation.viewport.size(), measurer);
		host.paint(canvas, 0);
		return canvas;
	}
	SDL_Event key(SDL_Keycode sym, Uint16 mod = 0)
	{
		SDL_Event e{};
		e.type = SDL_EVENT_KEY_DOWN;
		e.key.key = sym;
		e.key.mod = mod;
		host.event(e);
		return e;
	}
	void mouse(Uint32 type, Point p)
	{
		SDL_Event e{};
		e.type = type;
		e.common.timestamp = SDL_MS_TO_NS(clock);
		if (type == SDL_EVENT_MOUSE_MOTION)
		{
			e.motion.x = p.x;
			e.motion.y = p.y;
		}
		else
		{
			e.button.button = SDL_BUTTON_LEFT;
			e.button.x = p.x;
			e.button.y = p.y;
		}
		host.event(e);
	}
	void click(Point p)
	{
		mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, p);
		mouse(SDL_EVENT_MOUSE_BUTTON_UP, p);
	}
	void finger(Uint32 type, Point p)
	{
		SDL_Event e{};
		e.type = type;
		e.tfinger.timestamp = SDL_MS_TO_NS(clock);
		e.tfinger.touchID = 1;
		e.tfinger.fingerID = 1;
		e.tfinger.x = float(p.x) / presentation.viewport.w;
		e.tfinger.y = float(p.y) / presentation.viewport.h;
		host.event(e);
	}
	// Let `ms` pass and give the host its frame.
	void advance(Uint32 ms)
	{
		clock += ms;
		host.update(clock);
	}
	// Frames until coasting and bouncing stop; returns the offsets visited.
	std::vector<int> settle(const std::string &key)
	{
		std::vector<int> visited;
		for (int i = 0; i < 1000 && host.animating(); ++i)
		{
			advance(16);
			visited.push_back(host.find(key)->scrollOffset() + host.find(key)->overscroll());
		}
		return visited;
	}
	// A finger flick upward: four 20 px moves 16 ms apart, then release.
	void flick(Point from)
	{
		finger(SDL_EVENT_FINGER_DOWN, from);
		for (int i = 1; i <= 4; ++i)
		{
			advance(16);
			finger(SDL_EVENT_FINGER_MOTION, {from.x, from.y - 20 * i});
		}
		finger(SDL_EVENT_FINGER_UP, {from.x, from.y - 80});
	}
	void wheel(int y, Point at)
	{
		SDL_Event motion{};
		motion.type = SDL_EVENT_MOUSE_MOTION;
		motion.motion.x = at.x;
		motion.motion.y = at.y;
		host.event(motion);
		SDL_Event e{};
		e.type = SDL_EVENT_MOUSE_WHEEL;
		e.wheel.y = y;
		host.event(e);
	}
};

void checkIconsAndTooltips()
{
	auto asset = std::make_shared<IconAsset>();
	asset->name = "settings";
	asset->rasters.push_back({24, {}}); // Identity only: recording never accesses raster pixels.
	Fixture decorative([&](const Presentation &) { return row({icon(asset), expandedSpacer()}); },
					   120, 60);
	auto decoration = decorative.paint();
	require(decoration.icons.front().bounds.w == 20, "standalone icon defaults to 20 points");
	decorative.presentation.unit = 2;
	decorative.host.setPresentation(decorative.presentation);
	require(decorative.paint().icons.front().bounds.w == 40, "icon size follows point scale");
	ButtonOptions gear;
	gear.icon = asset;
	gear.iconSize = 24;
	gear.accessibleLabel = "Settings";
	gear.tooltip = "Settings";
	int clicks = 0;
	Fixture f(
		[&](const Presentation &p)
		{
			return row(
				{width(p.pt(48), button("gear", "", [&] { ++clicks; }, gear)), expandedSpacer()});
		},
		200, 100, true);
	require(f.host.find("gear")->accessibleText() == "Settings",
			"icon button retains its accessible name");
	require(f.host.bounds("gear").w == 48 && f.host.bounds("gear").h >= 48,
			"gear keeps a full touch target");
	auto painted = f.paint();
	require(painted.icons.size() == 1 && painted.icons[0].bounds.w == 24,
			"gear paints at 24 points");
	require(f.host.bounds("gear").contains(painted.icons[0].bounds), "icon stays inside button");
	f.host.focus("gear", true);
	f.key(SDLK_SPACE);
	require(clicks == 1, "icon button activates from keyboard");
	f.click({24, 24});
	require(clicks == 2, "icon button activates from pointer");
	auto missing = std::make_shared<IconAsset>();
	gear.icon = missing;
	Fixture fallback([&](const Presentation &) { return button("missing", "", [] {}, gear); }, 200,
					 100);
	require(fallback.paint().texts.front().second == "Settings",
			"missing icon restores translated text");
	bool rejected = false;
	try
	{
		ButtonOptions unnamed;
		unnamed.icon = asset;
		button("bad", "", [] {}, unnamed);
	}
	catch (const std::invalid_argument &)
	{
		rejected = true;
	}
	require(rejected, "icon-only buttons must be named");
	gear.icon = asset;
	gear.iconSize = 20;
	Fixture combined([&](const Presentation &)
					 { return button("combined", "A long translated label", [] {}, gear); }, 110,
					 160);
	auto combinedPaint = combined.paint();
	require(combinedPaint.icons.size() == 1 && combinedPaint.texts.size() > 1,
			"label wraps beside icon");
	for (const auto &line : combinedPaint.texts)
		require(line.first.x >= combinedPaint.icons[0].bounds.right() + 6,
				"text reserves the icon and gap");
	for (int state = 0; state < 4; ++state)
	{
		ButtonOptions options;
		options.icon = asset;
		options.enabled = state != 0;
		options.primary = state == 1;
		options.selected = state == 2;
		options.danger = state == 3;
		Fixture colours([&](const Presentation &)
						{ return button("colour", "Label", [] {}, options); }, 200, 100);
		const auto c = colours.paint().icons.front().color;
		const auto expected = state == 0   ? theme.palette.muted
							  : state == 1 ? theme.palette.accentInk
							  : state == 3 ? theme.palette.danger
										   : theme.palette.ink;
		require(c.r == expected.r && c.g == expected.g && c.b == expected.b,
				"icon inherits button ink state");
	}
	Fixture tip([&](const Presentation &)
				{ return row({width(48, button("tip", "", [] {}, gear)), expandedSpacer()}); }, 100,
				80);
	tip.host.focus("tip", true);
	auto draw = [&](Uint32 tick)
	{
		RecordingCanvas canvas({100, 80}, measurer);
		tip.host.paint(canvas, tick);
		return canvas;
	};
	require(draw(100).texts.empty() && draw(699).texts.empty(), "tooltip waits 600ms");
	auto shown = draw(700);
	require(shown.texts.size() == 1 && shown.texts[0].second == "Settings",
			"keyboard focus shows translated tooltip");
	require(shown.texts[0].first.x >= 0 && shown.texts[0].first.y >= 0 &&
				shown.texts[0].first.x + measurer.width(FontRole::Support, "Settings") <= 100,
			"tooltip remains in safe viewport");
	tip.key(SDLK_SPACE);
	require(draw(1400).texts.empty(), "activation suppresses tooltip until target changes");
	tip.host.focus("", false);
	draw(1401);
	SDL_Event motion{};
	motion.type = SDL_EVENT_MOUSE_MOTION;
	motion.motion.x = 15;
	motion.motion.y = 15;
	tip.host.event(motion);
	require(draw(1500).texts.empty(), "hover starts a new timer");
	require(draw(2100).texts.size() == 1, "pointer hover shows tooltip");
	SDL_Event press{};
	press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	press.button.button = SDL_BUTTON_LEFT;
	press.button.x = 15;
	press.button.y = 15;
	tip.host.event(press);
	draw(2101);
	press.type = SDL_EVENT_MOUSE_BUTTON_UP;
	tip.host.event(press);
	require(draw(2800).texts.empty(), "pointer activation hides tooltip even after pressed frame");
	motion.motion.x = 99;
	motion.motion.y = 79;
	tip.host.event(motion);
	require(draw(2200).texts.empty(), "pointer departure hides tooltip");
	tip.host.focus("tip", true);
	draw(2300);
	PopupSpec popup;
	popup.anchor = tip.host.bounds("tip");
	popup.options = {"Choice"};
	tip.host.openPopup(popup);
	auto popupPaint = draw(3000);
	for (const auto &text : popupPaint.texts)
		require(text.second != "Settings", "popup hides background tooltip");
}

void checkGeometry()
{
	Constraints c = Constraints::loose({100, 50});
	require(c.clamp({200, 20}) == Size{100, 20}, "clamp bounds width");
	require(!Constraints{}.boundedW(), "default constraints are unbounded");
	require(Rect{0, 0, 10, 10}.contains(Point{9, 9}) && !Rect{0, 0, 10, 10}.contains(Point{10, 9}),
			"rect containment is half open");
	require(Rect{0, 0, 10, 10}.intersect({5, 5, 10, 10}) == Rect{5, 5, 5, 5}, "rect intersection");
	require(Rect{0, 0, 10, 10}.unite({5, 5, 10, 10}) == Rect{0, 0, 15, 15}, "rect union");
}

void checkText()
{
	const auto lines = wrapText(measurer, FontRole::Body, "one two three four", 8 * 10);
	require(lines.size() == 2 && lines[0] == "one two" && lines[1] == "three four", "word wrap");
	const auto forced = wrapText(measurer, FontRole::Body, "abcdefghijkl", 8 * 5);
	require(forced.size() == 3 && forced[0] == "abcde", "unspaced runs break by glyph");
	const auto newline = wrapText(measurer, FontRole::Body, "a\nb", 800);
	require(newline.size() == 2, "explicit newline breaks");
	const auto cjk = wrapText(measurer, FontRole::Body, "日本語。テスト", 8 * 3);
	require(cjk.size() >= 2 && cjk[0] == "日本" && cjk[1].rfind("語。", 0) == 0,
			"closing punctuation stays with its glyph");
	require(ellipsize(measurer, FontRole::Body, "abcdefgh", 8 * 4) == "abc…", "ellipsis fits width");
	require(ellipsize(measurer, FontRole::Body, "abc", 8 * 4) == "abc", "short text untouched");
	require(nextGlyph("é", 0) == 2 && previousGlyph("aé", 3) == 1, "utf8 stepping");
}

void checkColumnAndFlex()
{
	Fixture f(
		[](const Presentation &)
		{
			return column({height(20, label("a")), expanded(label("b")), height(30, label("c"))},
						  {10});
		},
		200, 200);
	require(f.host.rootBounds() == Rect{0, 0, 200, 200}, "root fills the safe rect");
	auto *root = f.host.root();
	require(root->children.size() == 3, "column keeps three children");
	require(root->children[0]->bounds == Rect{0, 0, 200, 20}, "first fixed child");
	require(root->children[1]->bounds == Rect{0, 30, 200, 130}, "flex child takes the remainder");
	require(root->children[2]->bounds == Rect{0, 170, 200, 30}, "last fixed child at the bottom");
}

void checkRowCrossAlign()
{
	Fixture f(
		[](const Presentation &)
		{
			return row({sized({50, 10}, label("a")), sized({50, 30}, label("b"))},
					   {0, CrossAlign::Center});
		},
		200, 100);
	auto *root = f.host.root();
	require(root->children[0]->bounds == Rect{0, 45, 50, 10}, "row centers the short child");
	require(root->children[1]->bounds == Rect{50, 35, 50, 30}, "row places the tall child");
}

void checkParagraphWrapsWithWidth()
{
	Fixture f([](const Presentation &) { return column({paragraph("one two three four five six")}); }, 8 * 10, 400);
	auto *text = f.host.root()->children[0].get();
	require(text->bounds.h == 3 * 16 + 2 * theme.lineGap, "paragraph grows to three lines");
	f.resize(8 * 30, 400);
	require(f.host.root()->children[0]->bounds.h == 16, "paragraph shrinks to one line after resize");
}

void checkWrapColumns()
{
	auto build = [](const Presentation &)
	{
		return wrap({button("a", "A", {}), button("b", "B", {}), button("c", "C", {})},
					{4, 100, 0, true});
	};
	Fixture narrow(build, 150, 400);
	auto *root = narrow.host.root();
	require(root->children[0]->bounds.w == 150 && root->children[1]->bounds.y > 0,
			"narrow wrap stacks one per row");
	Fixture wide(build, 220, 400);
	root = wide.host.root();
	require(root->children[0]->bounds.y == root->children[1]->bounds.y &&
				root->children[2]->bounds.y > 0,
			"wide wrap fits two per row");
	require(root->children[0]->bounds.w == root->children[1]->bounds.w, "cells share width");
}

void checkFooterFolds()
{
	auto build = [](const Presentation &)
	{
		std::vector<Element> actions;
		for (int i = 0; i < 6; ++i)
			actions.push_back(button("act" + std::to_string(i), "Action", {}));
		return footer(scroll("body", paragraph("body text")), wrap(actions, {4, 200}));
	};
	Fixture tall(build, 300, 600);
	auto *root = tall.host.root();
	require(root->children[1]->bounds.bottom() == 600, "actions pinned to the bottom when they fit");
	require(root->children[0]->bounds.bottom() < root->children[1]->bounds.y, "body sits above actions");
	Fixture shortHost(build, 300, 220);
	root = shortHost.host.root();
	// Folded, the arranged tree is one page-level scroll holding body then actions.
	auto *fold = shortHost.host.find("footer/fold");
	require(fold && root->children.size() == 1 && root->children[0].get() == fold,
			"oversized actions join one page-level scroll");
	auto &flow = *fold->children[0];
	require(flow.children[1]->bounds.y == flow.children[0]->bounds.bottom() + theme.gap,
			"oversized actions fold into the flow below the body");
}

void checkScrollClampAndWheel()
{
	auto build = [](const Presentation &)
	{
		std::vector<Element> rows;
		for (int i = 0; i < 20; ++i)
			rows.push_back(height(30, label("row " + std::to_string(i))));
		return scroll("list", column(rows, {0}));
	};
	Fixture f(build, 200, 100);
	auto *sc = f.host.find("list");
	require(sc && sc->scrollMaximum() == 600 - 100, "scroll maximum is content minus viewport");
	f.wheel(-3, {50, 50});
	require(sc->scrollOffset() == 3 * f.host.metrics().control, "wheel scrolls by control heights");
	f.host.find("list")->scrollBy(100000, f.host);
	f.host.layoutIfNeeded();
	require(f.host.find("list")->scrollOffset() == 500, "offset clamps to the maximum");
	f.host.invalidate();
	f.host.layoutIfNeeded();
	require(f.host.find("list")->scrollOffset() == 500, "offset survives a rebuild");
	auto canvas = f.paint();
	for (const auto &t : canvas.texts)
		require(t.first.y >= -30 && t.first.y < 100, "only visible rows are painted near the viewport");
	// Rows above the viewport are still laid out but clipped.
	require(f.host.root()->hitTest({50, 50}, [](const Node &n) { return n.name() == std::string("label"); }) != nullptr,
			"hit test finds a clipped row inside the viewport");
	require(f.host.root()->hitTest({50, 150}, [](const Node &n) { return true; }) == nullptr,
			"hit test rejects points outside the scroll bounds");
}

void checkTapVersusPan()
{
	int taps = 0;
	auto build = [&](const Presentation &)
	{
		std::vector<Element> rows;
		for (int i = 0; i < 20; ++i)
			rows.push_back(button("b" + std::to_string(i), "B", [&] { ++taps; }, {false, false, true, false, false, false, SDLK_UNKNOWN, FontRole::Body, 40}));
		return scroll("list", column(rows, {0}));
	};
	Fixture f(build, 200, 100, true);
	f.finger(SDL_EVENT_FINGER_DOWN, {50, 20});
	f.finger(SDL_EVENT_FINGER_UP, {50, 20});
	require(taps == 1, "a tap activates the button under the finger");
	f.finger(SDL_EVENT_FINGER_DOWN, {50, 80});
	f.advance(16);
	f.finger(SDL_EVENT_FINGER_MOTION, {50, 40});
	f.advance(60); // the finger rests before lifting: no momentum
	f.finger(SDL_EVENT_FINGER_UP, {50, 40});
	require(taps == 1, "a drag scrolls instead of tapping");
	require(f.host.find("list")->scrollOffset() == 40, "drag distance becomes scroll offset");
	require(!f.host.animating(), "a rested finger leaves no momentum");
	f.finger(SDL_EVENT_FINGER_DOWN, {50, 20});
	SDL_Event lost{};
	lost.type = SDL_EVENT_WINDOW_RESIZED;
	lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
	f.host.event(lost);
	f.finger(SDL_EVENT_FINGER_UP, {50, 20});
	require(taps == 1, "focus loss cancels a held press");
}

void checkPressSurvivesResize()
{
	// A release already queued when the window resizes still lands on the
	// element it was pressed on.
	int clicks = 0;
	Fixture f([&](const Presentation &) { return column({button("go", "Go", [&] { ++clicks; })}); }, 300, 100);
	SDL_Event e{};
	e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	e.button.button = SDL_BUTTON_LEFT;
	e.button.x = 20;
	e.button.y = 10;
	f.host.event(e);
	f.resize(320, 120);
	e.type = SDL_EVENT_MOUSE_BUTTON_UP;
	f.host.event(e);
	require(clicks == 1, "a press survives a resize before its release");
}

void checkFocusAndKeyboard()
{
	std::vector<std::string> activated;
	Fixture f(
		[&](const Presentation &)
		{
			return column({button("one", "One", [&] { activated.push_back("one"); }),
						   button("two", "Two", [&] { activated.push_back("two"); }, {false, false, false}),
						   button("three", "Three", [&] { activated.push_back("three"); }, {true, false, true, false, false, false, SDLK_RETURN}),
						   button("cancel", "Cancel", [&] { activated.push_back("cancel"); }, {false, false, true, false, false, false, SDLK_ESCAPE})});
		},
		300, 400);
	require(f.host.focusOrder() == std::vector<std::string>{"one", "three", "cancel"}, "disabled buttons are skipped");
	f.key(SDLK_TAB);
	require(f.host.focused() == "one" && f.host.keyboardFocused(), "tab focuses the first button");
	f.key(SDLK_TAB);
	f.key(SDLK_TAB);
	f.key(SDLK_TAB);
	require(f.host.focused() == "one", "tab wraps around");
	f.key(SDLK_TAB, SDL_KMOD_LSHIFT);
	require(f.host.focused() == "cancel", "shift-tab moves backwards");
	f.key(SDLK_RETURN);
	require(activated == std::vector<std::string>{"cancel"}, "enter activates the focused button");
	f.host.focus("", false);
	f.key(SDLK_RETURN);
	require(activated.back() == "three", "enter without focus fires the return shortcut");
	f.key(SDLK_ESCAPE);
	require(activated.back() == "cancel", "escape fires the escape shortcut");
	f.click({150, 10});
	require(activated.back() == "one", "click activates the button under the pointer");
	f.click(f.host.bounds("two").center());
	require(activated.back() == "one", "disabled buttons ignore clicks");
}

void checkChoicePopup()
{
	int picked = -1;
	Fixture f(
		[&](const Presentation &)
		{
			return column({choice("pick", {"Alpha", "Beta", "Gamma"}, 1, [&](int v) { picked = v; }),
						   button("other", "Other", {})});
		},
		300, 400);
	f.click(f.host.bounds("pick").center());
	require(f.host.popupOpen(), "activating a choice opens its popup");
	require(f.host.find("popup/2") != nullptr, "popup lists every option");
	f.key(SDLK_DOWN);
	f.key(SDLK_RETURN);
	require(picked == 2 && !f.host.popupOpen(), "keyboard picks the highlighted option and closes");
	f.click(f.host.bounds("pick").center());
	f.click({299, 399});
	require(!f.host.popupOpen() && picked == 2, "clicking outside dismisses without picking");
}

void checkTextField()
{
	std::string model = "ab";
	Fixture f([&](const Presentation &) { return textField("name", model, [&](const std::string &v) { model = v; }); }, 300, 100);
	f.click({20, 20});
	require(f.host.editing() == "name", "tapping a field starts editing");
	SDL_Event text{};
	text.type = SDL_EVENT_TEXT_INPUT;
	text.text.text = "c";
	f.host.event(text);
	require(model == "abc", "text input appends at the cursor");
	f.key(SDLK_LEFT);
	f.key(SDLK_BACKSPACE);
	require(model == "ac", "backspace removes before the cursor after moving left");
	f.host.layoutIfNeeded();
	require(f.host.state("name").cursor == 1, "cursor position persists across rebuilds");
	f.key(SDLK_ESCAPE);
	require(f.host.editing().empty(), "escape ends editing");
}

void checkListView()
{
	int selected = 0;
	std::vector<std::string> items;
	for (int i = 0; i < 30; ++i)
		items.push_back("item " + std::to_string(i));
	Fixture f([&](const Presentation &) { return column({listView("files", items, selected, [&](int i) { selected = i; }, {{}, {}, {}, {}, {}, 5})}); }, 200, 400);
	auto *list = f.host.find("files");
	require(list && list->bounds.h == 5 * f.host.metrics().control, "list height follows visible rows");
	f.click({50, list->bounds.y + f.host.metrics().control + 2});
	require(selected == 1, "clicking a row selects it");
	f.key(SDLK_DOWN);
	require(selected == 2, "down arrow moves the selection");
	selected = 29;
	f.host.invalidate();
	f.host.layoutIfNeeded();
	require(f.host.find("files")->scrollOffset() > 0, "selection scrolls into view");
}

void checkAdaptiveAndField()
{
	Fixture f([](const Presentation &) { return form({field("Player name", textField("name", "x", {}), {"Shown in games"})}); }, 800, 400);
	auto nameWide = f.host.bounds("name");
	f.resize(300, 400);
	auto nameNarrow = f.host.bounds("name");
	require(nameWide.x > 0 && nameNarrow.x == 0, "field stacks on narrow widths and sits inline on wide ones");
	int chosen = 0;
	Fixture a([&](const Presentation &) { return adaptive([&](const LayoutContext &, Size available) { chosen = available.w >= 500 ? 2 : 1; return label("x"); }); }, 800, 400);
	require(chosen == 2, "adaptive sees the wide width");
	a.resize(300, 400);
	require(chosen == 1, "adaptive re-chooses after resize");
	// Fields inside an adaptive subtree only exist after layout: editing must
	// survive the rebuild each edit causes.
	std::string model = "x";
	Fixture b([&](const Presentation &) { return adaptive([&](const LayoutContext &, Size) { return textField("name", model, [&](const std::string &v) { model = v; }); }); }, 300, 100);
	b.click({20, 20});
	require(b.host.editing() == "name", "tapping a field inside adaptive starts editing");
	SDL_Event text{};
	text.type = SDL_EVENT_TEXT_INPUT;
	text.text.text = "y";
	b.host.event(text);
	require(model == "xy", "text input reaches a field inside adaptive");
	b.host.layoutIfNeeded();
	require(b.host.editing() == "name", "editing survives the rebuild after an edit");
}

Host::Builder buttonList(int &taps)
{
	return [&taps](const Presentation &)
	{
		std::vector<Element> rows;
		for (int i = 0; i < 20; ++i)
			rows.push_back(button("b" + std::to_string(i), "B", [&taps] { ++taps; }, {false, false, true, false, false, false, SDLK_UNKNOWN, FontRole::Body, 40}));
		return scroll("list", column(rows, {0}));
	};
}

void checkFlingContinues()
{
	int taps = 0;
	Fixture f(buttonList(taps), 200, 100, true);
	f.flick({50, 90});
	auto *list = f.host.find("list");
	require(list->scrollOffset() == 80, "the drag itself moved the content");
	require(f.host.animating(), "a flick keeps the content moving after release");
	f.advance(16);
	require(f.host.find("list")->scrollOffset() > 80, "the content coasts on the next frame");
	const auto path = f.settle("list");
	require(!f.host.animating() && !path.empty(), "coasting ends");
	for (std::size_t i = 1; i < path.size(); ++i)
		require(path[i] >= path[i - 1], "coasting never reverses");
	const int rest = f.host.find("list")->scrollOffset();
	require(rest > 80 && rest <= f.host.find("list")->scrollMaximum(), "the content rests further along, inside the range");
	require(f.host.find("list")->overscroll() == 0, "no stretch remains after coasting");
	f.host.invalidate();
	f.host.layoutIfNeeded();
	require(f.host.find("list")->scrollOffset() == rest, "the resting offset survives a rebuild");
	require(taps == 0, "a flick never taps");
	// Content that stops mid-way keeps its offset on later frames.
	f.advance(500);
	require(f.host.find("list")->scrollOffset() == rest, "idle content stays put");
}

void checkOverscrollSpringsBack()
{
	int taps = 0;
	Fixture f(buttonList(taps), 200, 100, true);
	f.host.find("list")->scrollBy(100000, f.host);
	f.host.layoutIfNeeded();
	const int end = f.host.find("list")->scrollMaximum();
	require(end > 0 && f.host.find("list")->scrollOffset() == end, "starts at the end");
	const int listTop = f.host.find("list")->bounds.y;
	f.finger(SDL_EVENT_FINGER_DOWN, {50, 90});
	f.advance(16);
	f.finger(SDL_EVENT_FINGER_MOTION, {50, 40});
	auto *list = f.host.find("list");
	require(list->scrollOffset() == end, "the clamped offset stays at the maximum");
	const int stretch = list->overscroll();
	require(stretch > 0 && stretch < 50, "pulling past the end stretches less than the finger moved");
	f.host.layoutIfNeeded();
	require(f.host.find("b0")->bounds.y == listTop - end - stretch, "children shift by the stretch");
	f.advance(16);
	f.finger(SDL_EVENT_FINGER_MOTION, {50, 10});
	require(f.host.find("list")->overscroll() > stretch, "pulling further stretches further");
	require(f.host.find("list")->overscroll() < 100, "the stretch stays inside the viewport");
	f.advance(60);
	f.finger(SDL_EVENT_FINGER_UP, {50, 10});
	require(f.host.animating(), "released stretched content springs back");
	// A rebuild mid-bounce persists only the clamped offset.
	f.host.invalidate();
	f.host.layoutIfNeeded();
	require(f.host.state("list").scroll == end, "persisted state never includes the stretch");
	require(f.host.find("list")->overscroll() == 0, "a rebuilt node starts unstretched");
	f.advance(16);
	require(f.host.find("list")->overscroll() > 0, "the bounce continues on the rebuilt node");
	const auto path = f.settle("list");
	for (std::size_t i = 1; i < path.size(); ++i)
		require(path[i] <= path[i - 1] + 1, "the spring never overshoots back");
	require(f.host.find("list")->overscroll() == 0, "the stretch is gone");
	require(f.host.find("list")->scrollOffset() == end, "the content rests at the end");
	f.host.layoutIfNeeded();
	require(f.host.find("b0")->bounds.y == listTop - end, "children return to their place");
	require(taps == 0, "stretching never taps");
}

void checkTouchStopsFling()
{
	int taps = 0;
	Fixture f(buttonList(taps), 200, 100, true);
	f.flick({50, 90});
	f.advance(16);
	require(f.host.animating(), "coasting");
	f.finger(SDL_EVENT_FINGER_DOWN, {50, 50});
	require(!f.host.animating(), "a touch stops the coasting content");
	const int held = f.host.find("list")->scrollOffset();
	f.advance(100);
	require(f.host.find("list")->scrollOffset() == held, "stopped content stays where the finger caught it");
	f.finger(SDL_EVENT_FINGER_UP, {50, 50});
	require(taps == 0, "the stopping touch is not a tap");
	require(!f.host.animating(), "nothing moves after the stopping touch lifts");
	f.finger(SDL_EVENT_FINGER_DOWN, {50, 50});
	f.finger(SDL_EVENT_FINGER_UP, {50, 50});
	require(taps == 1, "the next touch taps as usual");
	// Wheel input also stops a fling and scrolls by control heights from there.
	f.flick({50, 90});
	f.advance(16);
	const int before = f.host.find("list")->scrollOffset();
	f.wheel(1, {50, 50});
	require(!f.host.animating(), "the wheel stops coasting");
	require(f.host.find("list")->scrollOffset() == before - f.host.metrics().control, "the wheel scrolls from where the content was");
	// Content moved by something else drops the animation instead of fighting it.
	f.flick({50, 90});
	f.advance(16);
	f.host.scrollIntoView("b0");
	f.host.layoutIfNeeded();
	f.advance(16);
	require(!f.host.animating(), "scrollIntoView ends coasting");
	require(f.host.find("list")->scrollOffset() == 0, "the programmatic position wins");
}

void checkScrollbarPressStopsFling()
{
	int taps = 0;
	Fixture f(buttonList(taps), 200, 100, true);
	f.flick({50, 90});
	f.advance(16);
	require(f.host.animating(), "coasting");
	auto *list = f.host.find("list");
	const Point track{list->bounds.right() - 2, list->bounds.y + 50};
	require(list->capturesPointer(track), "the scrollbar track captures the pointer");
	f.finger(SDL_EVENT_FINGER_DOWN, track);
	require(!f.host.animating(), "grabbing the scrollbar stops coasting");
	f.finger(SDL_EVENT_FINGER_UP, track);
	require(!f.host.animating(), "the thumb release leaves the content still");
}

void checkMouseDragDoesNotFling()
{
	int taps = 0;
	Fixture f(buttonList(taps), 200, 100, false);
	f.mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, {50, 90});
	for (int i = 1; i <= 4; ++i)
	{
		f.advance(16);
		f.mouse(SDL_EVENT_MOUSE_MOTION, {50, 90 - 20 * i});
	}
	f.mouse(SDL_EVENT_MOUSE_BUTTON_UP, {50, 10});
	require(f.host.find("list")->scrollOffset() == 80, "a mouse drag scrolls by the pointer distance");
	require(!f.host.animating(), "a mouse drag has no momentum");
	f.advance(100);
	require(f.host.find("list")->scrollOffset() == 80, "the content stays where the mouse left it");
	f.host.find("list")->scrollBy(100000, f.host);
	f.host.layoutIfNeeded();
	f.mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, {50, 90});
	f.advance(16);
	f.mouse(SDL_EVENT_MOUSE_MOTION, {50, 40});
	require(f.host.find("list")->overscroll() == 0, "a mouse drag past the end does not stretch");
	f.mouse(SDL_EVENT_MOUSE_BUTTON_UP, {50, 40});
	require(taps == 0, "mouse drags never tap");
}

void checkListAndTextOverscroll()
{
	int selected = 0;
	std::vector<std::string> items;
	for (int i = 0; i < 30; ++i)
		items.push_back("item " + std::to_string(i));
	Fixture f([&](const Presentation &) { return column({listView("files", items, selected, [&](int i) { selected = i; }, {{}, {}, {}, {}, {}, 5})}); }, 200, 400, true);
	auto *list = f.host.find("files");
	list->scrollBy(100000, f.host);
	f.host.layoutIfNeeded();
	list = f.host.find("files");
	const int maximum = list->scrollMaximum();
	require(maximum > 0, "the list overflows");
	const int lastRowTop = list->subTargets().back().bounds.y;
	f.finger(SDL_EVENT_FINGER_DOWN, {50, list->bounds.y + 90});
	f.advance(16);
	f.finger(SDL_EVENT_FINGER_MOTION, {50, list->bounds.y + 40});
	list = f.host.find("files");
	require(list->scrollOffset() == maximum && list->overscroll() > 0, "a list stretches past its last row");
	require(list->subTargets().back().bounds.y == lastRowTop - list->overscroll(), "list rows shift by the stretch");
	f.advance(60);
	f.finger(SDL_EVENT_FINGER_UP, {50, list->bounds.y + 40});
	f.settle("files");
	require(f.host.find("files")->overscroll() == 0 && f.host.find("files")->scrollOffset() == maximum, "the list springs back");
	require(selected == 0, "stretching a list selects nothing");

	std::string text;
	for (int i = 0; i < 40; ++i)
		text += "line " + std::to_string(i) + "\n";
	Fixture t([&](const Presentation &) { return column({textEditor("log", text, {}, {true, 4})}); }, 200, 400, true);
	auto *editor = t.host.find("log");
	editor->scrollBy(100000, t.host);
	t.host.layoutIfNeeded();
	editor = t.host.find("log");
	require(editor->scrollMaximum() > 0, "the log overflows");
	auto before = t.paint();
	t.finger(SDL_EVENT_FINGER_DOWN, {50, editor->bounds.y + 60});
	t.advance(16);
	t.finger(SDL_EVENT_FINGER_MOTION, {50, editor->bounds.y + 20});
	editor = t.host.find("log");
	require(editor->overscroll() > 0, "a text log stretches past its last line");
	auto during = t.paint();
	require(!before.texts.empty() && !during.texts.empty(), "lines are painted");
	require(during.texts.back().first.y == before.texts.back().first.y - editor->overscroll(), "painted lines shift by the stretch");
	t.advance(60);
	t.finger(SDL_EVENT_FINGER_UP, {50, editor->bounds.y + 20});
	t.settle("log");
	require(t.host.find("log")->overscroll() == 0, "the log springs back");
}

// Whatever the width and text size, a control's measured size holds the text
// it paints (see glob2test::textSpill), so larger text never spills into a neighbour.
void checkTextStaysInControls()
{
	auto gear = std::make_shared<IconAsset>();
	gear->name = "gear";
	gear->rasters.push_back({24, {}});
	const std::string longText = "Language & player preferences";
	auto build = [&](const Presentation &p)
	{
		ButtonOptions nav;
		nav.flat = true;
		nav.alignLeft = true;
		nav.minHeight = 42;
		nav.icon = gear;
		ButtonOptions iconOnly;
		iconOnly.icon = gear;
		iconOnly.accessibleLabel = "Gear";
		SliderOptions slide;
		slide.caption = longText;
		slide.valueText = "50%";
		StepperOptions step;
		step.valueText = "Three colonies";
		ListOptions list;
		list.visibleRows = 3;
		std::vector<Element> rows{
			button("nav/0", "Display & graphics", {}, nav),
			button("nav/1", "Gameplay", {}, nav),
			button("nav/2", longText, {}, nav),
			button("plain", longText, {}),
			button("icon", "", {}, iconOnly),
			row({expanded(button("pair/0", "Players & Teams", {})), expanded(button("pair/1", "Game Rules", {}))}, {p.pt(4)}),
			toggle("toggle", longText, true, {}),
			segments("segments", {"Text size 100%", "Text size 125%", "Text size 150%"}, 0, {}),
			choice("choice", {longText, "Short"}, 0, {}),
			chooser("chooser", longText, {}),
			stepper("stepper", 3, 0, 10, {}, step),
			slider("slider", 5, 0, 10, {}, slide),
			textField("field", longText, {}),
			listView("list", {longText, "Two", "Three"}, 0, {}, list),
		};
		return column(std::move(rows), {p.pt(4)});
	};
	for (bool touch : {false, true})
		// Text sizes 100%, 150% and 175%, then a font whose lines run taller than the
		// control height (platform rasterizers report different line heights).
		for (auto [glyph, line] : {std::pair{8, 16}, {12, 24}, {14, 28}, {10, 40}})
			for (int width : {140, 200, 280, 420})
			{
				FixedTextMeasurer sized(glyph, line);
				Host host(theme, build);
				host.setMeasurer(&sized);
				host.setPresentation(Presentation::forSurface(width, 3000, 1, touch));
				host.layoutIfNeeded();
				RecordingCanvas canvas({width, 3000}, sized);
				host.paint(canvas, 0);
				const auto spill = glob2test::textSpill(canvas, host.interactiveNodes());
				GLOB2_REQUIRE(spill.empty(), spill + " (touch=" + std::to_string(touch) + " glyph=" + std::to_string(glyph) +
												 " line=" + std::to_string(line) + " width=" + std::to_string(width) + ")");
			}
}

void checkInvariants()
{
	Fixture f(
		[](const Presentation &p)
		{
			std::vector<Element> actions{button("ok", "OK", {}), button("cancel", "Cancel", {})};
			return footer(scroll("body", column({paragraph("Some text"), toggle("t", "Toggle", true, {}), stepper("s", 3, 0, 10, {}), slider("sl", 5, 0, 10, {})})), wrap(actions));
		},
		320, 568, true);
	for (auto *node : f.host.interactiveNodes())
	{
		require(f.presentation.safe.contains(node->bounds), "interactive nodes stay inside the safe rect");
		require(node->bounds.h >= f.host.metrics().minTarget, "touch targets meet the minimum height");
	}
}
} // namespace

TEST_SUITE("UILayout")
{
	TEST_CASE("icons and tooltips")
	{
		checkIconsAndTooltips();
	}
	TEST_CASE("geometry") { checkGeometry(); }
	TEST_CASE("text") { checkText(); }
	TEST_CASE("column and flex") { checkColumnAndFlex(); }
	TEST_CASE("row cross align") { checkRowCrossAlign(); }
	TEST_CASE("paragraph wraps with width") { checkParagraphWrapsWithWidth(); }
	TEST_CASE("wrap columns") { checkWrapColumns(); }
	TEST_CASE("footer folds") { checkFooterFolds(); }
	TEST_CASE("scroll clamp and wheel") { checkScrollClampAndWheel(); }
	TEST_CASE("tap versus pan") { checkTapVersusPan(); }
	TEST_CASE("fling continues after release") { checkFlingContinues(); }
	TEST_CASE("overscroll springs back") { checkOverscrollSpringsBack(); }
	TEST_CASE("touch wheel and programmatic scroll stop a fling") { checkTouchStopsFling(); }
	TEST_CASE("scrollbar press stops a fling") { checkScrollbarPressStopsFling(); }
	TEST_CASE("mouse drag does not fling") { checkMouseDragDoesNotFling(); }
	TEST_CASE("list and text overscroll") { checkListAndTextOverscroll(); }
	TEST_CASE("press survives resize") { checkPressSurvivesResize(); }
	TEST_CASE("focus and keyboard") { checkFocusAndKeyboard(); }
	TEST_CASE("choice popup") { checkChoicePopup(); }
	TEST_CASE("text field") { checkTextField(); }
	TEST_CASE("list view") { checkListView(); }
	TEST_CASE("adaptive and field") { checkAdaptiveAndField(); }
	TEST_CASE("invariants") { checkInvariants(); }
	TEST_CASE("text stays inside its control at every size") { checkTextStaysInControls(); }
}
