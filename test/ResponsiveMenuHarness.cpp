// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalContainer.h"
#include "MainMenuScreen.h"
#include "MenuColony.h"
#include "LobbyControls.h"
#include <ScreenStack.h>
#include <Toolkit.h>
#include <GUIList.h>
#include <GUICheckList.h>
#include <cstdio>
#include <stdexcept>

GlobalContainer *globalContainer = nullptr;
void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
struct CountingTheme : FrontendTheme
{
	int frames = 0;
	void onFrame() override
	{
		++frames;
		FrontendTheme::onFrame();
	}
};
struct ResponsiveMenuHarness
{
	static SDL_Rect action(MainMenuScreen &menu, const std::string &id)
	{
		for (const auto &hit : menu.mobileControls->hits)
			if (hit.id == id)
				return hit.box;
		throw std::runtime_error("Missing menu action: " + id);
	}
};
struct Menu : MainMenuScreen
{
	std::vector<int> selected;
	void verifyWrapping()
	{
		for (auto *widget : widgets)
			if (auto *button = dynamic_cast<GAGGUI::TextButton *>(widget))
			{
				button->setText("Très longue description traduite du menu et de toutes les "
								"possibilités disponibles");
				const auto lines = button->wrappedLines(180);
				require(lines.size() > 1 && button->wrappedHeight(204) > 48,
						"Long translated labels must wrap and grow");
				for (const auto &line : lines)
				{
					button->setText(line);
					require(button->textWidth() <= 180, "Wrapped translation exceeds its button");
				}
				break;
			}
	}
	void onAction(GAGGUI::Widget *, GAGGUI::Action action, int choice, int) override
	{
		if (action == GAGGUI::BUTTON_RELEASED)
			selected.push_back(choice);
	}
};
SDL_Event finger(Uint32 type, float x, float y, SDL_FingerID id = 1)
{
	SDL_Event event{};
	event.type = type;
	event.tfinger.touchId = 10;
	event.tfinger.fingerId = id;
	event.tfinger.x = x / 320;
	event.tfinger.y = y / 568;
	return event;
}
// Opt-in list row sizing must affect hit testing as well as pixels. Otherwise
// roomy touch rows can still select their old, tightly spaced desktop indices.
void verifyTouchListRows(GAGCore::GraphicContext *gfx)
{
	struct ListScreen : GAGGUI::Screen
	{
		void onAction(GAGGUI::Widget *, GAGGUI::Action, int, int) override {}
	} screen;
	auto *list = new GAGGUI::List(8, 8, 240, 140, ALIGN_LEFT, ALIGN_TOP, "standard");
	auto *checks =
		new GAGGUI::CheckList(8, 160, 240, 140, ALIGN_LEFT, ALIGN_TOP, "standard", false);
	for (const char *value : {"First", "Second", "Third"})
	{
		list->addText(value);
		checks->addItem(value, false);
	}
	list->setMinimumRowHeight(44);
	checks->setMinimumRowHeight(44);
	screen.addWidget(list);
	screen.addWidget(checks);
	screen.beginExecution(gfx);
	screen.drawExecution();
	SDL_Event click{};
	click.type = SDL_MOUSEBUTTONDOWN;
	click.button.button = SDL_BUTTON_LEFT;
	click.button.x = 100;
	click.button.y = 8 + 44 + 20;
	screen.handleExecutionEvent(click);
	require(list->getSelectionIndex() == 1,
			"Touch list hit testing must use the displayed row height");
	click.button.y = 160 + 20;
	screen.handleExecutionEvent(click);
	require(checks->isChecked(0), "Touch checklist must toggle from its whole row");
	list->setMinimumRowHeight(0);
	screen.drawExecution();
	click.button.y = 8 + GAGCore::Toolkit::getFont("standard")->getStringHeight(" ") + 5;
	screen.handleExecutionEvent(click);
	require(list->getSelectionIndex() == 1,
			"Default list row height must restore desktop hit testing");
}
int main()
{
	if (!SDL_getenv("GLOB2_USER_DATA_DIR"))
		return 2;
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	SDL_setenv("GLOB2_MOBILE_UI", "1", 1);
	try
	{
		globalContainer = new GlobalContainer("glob2-responsive-menu-test");
		globalContainer->settings.screenWidth = 800;
		globalContainer->settings.screenHeight = 600;
		globalContainer->settings.screenFlags =
			GAGCore::GraphicContext::PORTABLEGPU | GAGCore::GraphicContext::RESIZABLE;
		globalContainer->settings.mute = true;
		globalContainer->load();
		auto theme = std::make_unique<CountingTheme>();
		auto *window = SDL_GetWindowFromID(globalContainer->gfx->windowID());
		require(window, "Fixture must own its only SDL window");
		SDL_SetWindowSize(window, 320, 568);
		SDL_Event resize{};
		resize.type = SDL_WINDOWEVENT;
		resize.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
		GAGCore::GraphicContext::translateMouseEvent(&resize);
		verifyTouchListRows(globalContainer->gfx);
		{
			GAGGUI::ScreenStack stack(*globalContainer->gfx);
			auto owned = std::make_unique<Menu>();
			auto *menu = owned.get();
			stack.push(std::move(owned));
			const auto frameCount = theme->frames;
			stack.frame(0, {});
			require(theme->frames == frameCount + 1,
					"Responsive frontend calls the theme hook exactly once per frame");
			require(globalContainer->gfx->getW() == 320 && globalContainer->gfx->getH() == 568,
					"Menu did not adopt portrait dimensions");
			const auto custom = ResponsiveMenuHarness::action(
				*menu, "menu/" + std::to_string(MainMenuScreen::CUSTOM));
			const float cx = custom.x + custom.w / 2, cy = custom.y + custom.h / 2;
			stack.frame(40, {finger(SDL_FINGERDOWN, cx, cy), finger(SDL_FINGERUP, cx, cy)});
			require(menu->selected == std::vector<int>{MainMenuScreen::CUSTOM},
					"Touch tap did not select custom game exactly once");
			SDL_Event mouse{};
			mouse.type = SDL_MOUSEBUTTONUP;
			mouse.button.which = SDL_TOUCH_MOUSEID;
			mouse.button.button = SDL_BUTTON_LEFT;
			mouse.button.x = 160;
			mouse.button.y = 80;
			stack.frame(80, {mouse});
			require(menu->selected.size() == 1, "Synthesized mouse event duplicated selection");
			stack.frame(120, {finger(SDL_FINGERDOWN, 160, 180), finger(SDL_FINGERMOTION, 160, 100),
							  finger(SDL_FINGERUP, 160, 100)});
			require(menu->selected.size() == 1, "Scroll drag selected a button");
			stack.frame(160, {finger(SDL_FINGERDOWN, cx, cy)});
			SDL_Event lost{};
			lost.type = SDL_WINDOWEVENT;
			lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
			stack.frame(200, {lost});
			stack.frame(240, {finger(SDL_FINGERUP, cx, cy)});
			require(menu->selected.size() == 1, "Canceled touch selected a button");
			globalContainer->gfx->printScreen("responsive-menu.bmp");
			stack.frame(280, {});
			struct Child : GAGGUI::Screen
			{
				void onAction(GAGGUI::Widget *, GAGGUI::Action, int, int) override {}
				void drawExecution() override {}
			};
			auto child = std::make_unique<Child>();
			auto *childProbe = child.get();
			stack.push(std::move(child));
			stack.frame(320, {});
#ifdef GLOB2_MOBILE
			// Native mobile expands the legacy canvas to the device aspect ratio.
			require(globalContainer->gfx->getW() == 800 && globalContainer->gfx->getH() == 1420,
					"Legacy child must retain its minimum size and portrait aspect ratio");
#else
			require(globalContainer->gfx->getW() == 800 && globalContainer->gfx->getH() == 600,
					"Child needs legacy coordinates before initialization");
#endif
			childProbe->endExecute(0);
			stack.frame(360, {});
			require(globalContainer->gfx->getW() == 320 && globalContainer->gfx->getH() == 568,
					"Resumed menu needs responsive coordinates");
			SDL_SetWindowSize(window, 568, 320);
			stack.frame(400, {resize});
			require(globalContainer->gfx->getW() == 568 && globalContainer->gfx->getH() == 320,
					"Menu rotation did not reflow");
			menu->verifyWrapping();
			const auto before = theme->colony->tick();
			const auto started = SDL_GetTicks();
			do
			{
				SDL_Delay(20);
				stack.frame(SDL_GetTicks(), {});
			} while (SDL_GetTicks() - started < 240);
			require(theme->colony->ready(), "Responsive frontend must load the live colony");
			require(theme->colony->tick() > before,
					"Responsive frontend must advance the live colony over wall time");
		}
		globalContainer->gfx->setResponsiveViewport(false);
#ifdef GLOB2_MOBILE
		require(globalContainer->gfx->getW() == 1065 && globalContainer->gfx->getH() == 600,
				"Legacy viewport must preserve the landscape aspect ratio");
#else
		require(globalContainer->gfx->getW() == 800 && globalContainer->gfx->getH() == 600,
				"Legacy viewport was not restored");
#endif
		theme.reset();
		delete globalContainer;
		globalContainer = nullptr;
		std::puts("PASS responsive menu: portrait layout, actual touch dispatch, duplicate "
				  "suppression, scrolling, cancellation, legacy restoration");
	}
	catch (const std::exception &error)
	{
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		return 1;
	}
}
