// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ui/Screen.h>
#include <memory>
#include <ui/Controls.h>
#include "FrontendTheme.h"

class MapPreview;

// Game-side bindings of the declarative UI framework: the frontend theme,
// the screen base that paints the live colony background, and shared builders.
namespace Glob2UI
{
using namespace GAGGUI::ui;

const Theme &frontendTheme();
const Theme &inGameTheme();

// Translated string lookup, "[key]" convention.
std::string tr(const std::string &key);

// Presentation text scale from the local settings.
double frontendTextScale(const Presentation &presentation);

// A frontend menu: paints the colony background and keeps the frontend theme
// active for its whole lifetime, so a menu created while gameplay winds down
// already presents as a menu.
class Screen : public UIScreen
{
  public:
	Screen();
	~Screen() override;

  protected:
	double textScale(const Presentation &presentation) const override;
	void paintBackground(Canvas &canvas) override;
	void beforePaint() override;
	// Whether the content sits on a paper panel; menus and forms do, full-window views do not.
	virtual bool panel() const { return true; }

  private:
	FrontendScope scope{true};
};

class Dialog : public UIDialog
{
  public:
	Dialog();

  protected:
	double textScale(const Presentation &presentation) const override;
};

// A modal over gameplay or the editor, in the in-match theme.
class InGameDialog : public UIDialog
{
  public:
	InGameDialog();

  protected:
	double textScale(const Presentation &presentation) const override;
};

// Layout helpers shared by frontend screens.
struct MenuAction
{
	std::string key, label;
	std::function<void()> action;
	bool primary = false;
	SDL_Keycode shortcut = SDLK_UNKNOWN;
	bool enabled = true;
};
// Title above a scrolling list of large actions (two columns when wide).
Element menu(const std::string &titleText, std::vector<MenuAction> actions, const Presentation &p);
// Horizontal action row that wraps on narrow layouts; the last action is the escape route.
Element actions(std::vector<MenuAction> actions, const Presentation &p);
// Panel-sized page: title, body and pinned actions inside a centered card.
Element page(const std::string &titleText, Element body, Element actionRow, const Presentation &p,
			 double maxWidthPoints = 640);
// Sprite frames cycled on wall time.
Element animation(const std::string &key, GAGCore::Sprite *sprite, int frames, int millisecondsPerFrame);
// Hosts a legacy MapPreview widget (owned by the screen) at `points` square, with
// drag, wheel zoom and retry forwarded to it.
Element mapPreview(const std::string &key, ::MapPreview &preview, double points, bool flexible = false);
} // namespace Glob2UI
