// SPDX-License-Identifier: GPL-3.0-or-later
// Layout-engine checks without fonts or a window: fixed-advance text, recording canvas.
#include "Glob2Test.h"
#include <string>
#include <functional>
#include <utility>
#include <ui/Containers.h>
#include <ui/Controls.h>
#include <ui/Host.h>
#include <BrowserTextInput.h>
#include <cstdio>
#include <vector>

using namespace GAGGUI::ui;

namespace
{
void require(bool condition, const char *message)
{
	GLOB2_REQUIRE(condition, message);
}

struct RecordingCanvas : Canvas
{
	Size extent;
	const TextMeasurer &text_;
	std::vector<Rect> clips;
	std::vector<std::pair<Point, std::string>> texts;
	int fills = 0;
	RecordingCanvas(Size extent, const TextMeasurer &measurer) : extent(extent), text_(measurer)
	{
		clips.push_back({0, 0, extent.w, extent.h});
	}
	Size size() const override { return extent; }
	const TextMeasurer &measurer() const override { return text_; }
	void fillRect(Rect, GAGCore::Color) override { ++fills; }
	void strokeRect(Rect, GAGCore::Color) override {}
	void fillRounded(Rect, int, GAGCore::Color) override { ++fills; }
	void line(Point, Point, GAGCore::Color) override {}
	void text(Point at, FontRole, const std::string &value, GAGCore::Color) override
	{
		texts.push_back({at, value});
	}
	void pushClip(Rect rect) override { clips.push_back(clips.back().intersect(rect)); }
	void popClip() override
	{
		if (clips.size() > 1)
			clips.pop_back();
	}
	Rect clip() const override { return clips.back(); }
	void drawSurface(Rect, GAGCore::DrawableSurface *, unsigned char) override {}
	void drawSprite(Point, GAGCore::Sprite *, int) override {}
	void transformed(double, Point, Rect, const std::function<void()> &paint) override { paint(); }
};

Theme theme;
FixedTextMeasurer measurer(8, 16);

struct Fixture
{
	Host host;
	Presentation presentation;
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
		e.type = SDL_KEYDOWN;
		e.key.keysym.sym = sym;
		e.key.keysym.mod = mod;
		host.event(e);
		return e;
	}
	void click(Point p)
	{
		SDL_Event e{};
		e.type = SDL_MOUSEBUTTONDOWN;
		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = p.x;
		e.button.y = p.y;
		host.event(e);
		e.type = SDL_MOUSEBUTTONUP;
		host.event(e);
	}
	void finger(Uint32 type, Point p)
	{
		SDL_Event e{};
		e.type = type;
		e.tfinger.touchId = 1;
		e.tfinger.fingerId = 1;
		e.tfinger.x = float(p.x) / presentation.viewport.w;
		e.tfinger.y = float(p.y) / presentation.viewport.h;
		host.event(e);
	}
	void wheel(int y, Point at)
	{
		SDL_Event motion{};
		motion.type = SDL_MOUSEMOTION;
		motion.motion.x = at.x;
		motion.motion.y = at.y;
		host.event(motion);
		SDL_Event e{};
		e.type = SDL_MOUSEWHEEL;
		e.wheel.y = y;
		host.event(e);
	}
};

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
	f.finger(SDL_FINGERDOWN, {50, 20});
	f.finger(SDL_FINGERUP, {50, 20});
	require(taps == 1, "a tap activates the button under the finger");
	f.finger(SDL_FINGERDOWN, {50, 80});
	f.finger(SDL_FINGERMOTION, {50, 40});
	f.finger(SDL_FINGERUP, {50, 40});
	require(taps == 1, "a drag scrolls instead of tapping");
	require(f.host.find("list")->scrollOffset() == 40, "drag distance becomes scroll offset");
	f.finger(SDL_FINGERDOWN, {50, 20});
	SDL_Event lost{};
	lost.type = SDL_WINDOWEVENT;
	lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
	f.host.event(lost);
	f.finger(SDL_FINGERUP, {50, 20});
	require(taps == 1, "focus loss cancels a held press");
}

void checkPressSurvivesResize()
{
	// A release already queued when the window resizes still lands on the
	// element it was pressed on.
	int clicks = 0;
	Fixture f([&](const Presentation &) { return column({button("go", "Go", [&] { ++clicks; })}); }, 300, 100);
	SDL_Event e{};
	e.type = SDL_MOUSEBUTTONDOWN;
	e.button.button = SDL_BUTTON_LEFT;
	e.button.x = 20;
	e.button.y = 10;
	f.host.event(e);
	f.resize(320, 120);
	e.type = SDL_MOUSEBUTTONUP;
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
	f.key(SDLK_TAB, KMOD_LSHIFT);
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
	text.type = SDL_TEXTINPUT;
	std::snprintf(text.text.text, sizeof text.text.text, "%s", "c");
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
	text.type = SDL_TEXTINPUT;
	std::snprintf(text.text.text, sizeof text.text.text, "%s", "y");
	b.host.event(text);
	require(model == "xy", "text input reaches a field inside adaptive");
	b.host.layoutIfNeeded();
	require(b.host.editing() == "name", "editing survives the rebuild after an edit");
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
	TEST_CASE("geometry") { checkGeometry(); }
	TEST_CASE("text") { checkText(); }
	TEST_CASE("column and flex") { checkColumnAndFlex(); }
	TEST_CASE("row cross align") { checkRowCrossAlign(); }
	TEST_CASE("paragraph wraps with width") { checkParagraphWrapsWithWidth(); }
	TEST_CASE("wrap columns") { checkWrapColumns(); }
	TEST_CASE("footer folds") { checkFooterFolds(); }
	TEST_CASE("scroll clamp and wheel") { checkScrollClampAndWheel(); }
	TEST_CASE("tap versus pan") { checkTapVersusPan(); }
	TEST_CASE("press survives resize") { checkPressSurvivesResize(); }
	TEST_CASE("focus and keyboard") { checkFocusAndKeyboard(); }
	TEST_CASE("choice popup") { checkChoicePopup(); }
	TEST_CASE("text field") { checkTextField(); }
	TEST_CASE("list view") { checkListView(); }
	TEST_CASE("adaptive and field") { checkAdaptiveAndField(); }
	TEST_CASE("invariants") { checkInvariants(); }
}
