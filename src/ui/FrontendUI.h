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

enum class UIIcon
{
	Settings,
	Editor,
	LoadGame,
	Display,
	Audio,
	Gameplay,
	Buildings,
	Controls,
	Player,
	CustomGame,
	Campaign,
	Tutorial,
	Online,
	LAN,
	Credits,
	Quit,
	Back,
	More,
	Send,
	Close,
	Refresh,
	Info,
	Experiments,
	// Online play
	Link,
	Copy,
	Share,
	Trophy,
	Robot,
	WifiOff,
	Signal,
	ShieldCheck,
	Server,
	Users,
	Map,
	Chat,
	Check,
	Plus,
	SignIn,
	Crown,
	Lock,
	ExternalLink,
	Download,
	Bolt,
	Code,
	Leave,
	Start,
	Rules,
	Spinner,
	Warning,
	Search,
	Upload,
	Heart,
	Count
};
IconRef uiIcon(UIIcon icon);
// Familiar toolbar actions: a named 48-point icon button on touch, text on pointer hosts.
Element compactButton(const std::string &key, const std::string &label, UIIcon icon,
					  std::function<void()> action, const Presentation &p,
					  ButtonOptions options = {});

// The paper look of the menus, the results and every other frontend screen.
const Theme &frontendTheme();
// The aubergine, cream and gold in-match look, shared with the HUD.
const Theme &inGameTheme();
// Kinds of surfaces; themeFor() maps each to its theme in one place, so moving a
// kind of surface to the other look is a one-line change.
enum class Surface
{
	// Menus, lobbies, settings and other screens outside a match.
	Frontend,
	// Dialogs over a match: the in-game menu, alliances, objectives, save/load.
	Match,
	// Dialogs over the map editor.
	Editor,
	// The after-game statistics screen.
	Results
};
const Theme &themeFor(Surface surface);
// Whether the touch (phone/tablet) presentation was requested for this run.
bool touchPresentation();

// Translated string lookup, "[key]" convention.
std::string tr(const std::string &key);

// Layout helpers shared by frontend screens.
struct MenuAction
{
	std::string key, label;
	std::function<void()> action;
	bool primary = false;
	SDL_Keycode shortcut = SDLK_UNKNOWN;
	bool enabled = true;
};
// A frontend menu: paints the colony background and keeps the frontend theme
// active for its whole lifetime, so a menu created while gameplay winds down
// already presents as a menu.
class Screen : public UIScreen
{
  public:
	Screen();
	~Screen() override;

  protected:
	void paintBackground(Canvas &canvas) override;
	void beforePaint() override;
	// Whether the content sits on a paper panel; menus and forms do, full-window views do not.
	virtual bool panel() const { return true; }
	// Menus read larger on big desktop windows while the interface scale follows the
	// desktop (ui::comfortScale); gameplay and its dialogs keep their sizes.
	void adjustPresentation(Presentation &presentation) override;

  private:
	FrontendScope scope{true};
};

class Dialog : public UIDialog
{
  public:
	Dialog();

  protected:
	bool scrim() const override { return false; }
};

// A modal over gameplay or the editor, in the theme themeFor() picks for its surface.
class InGameDialog : public UIDialog
{
  public:
	explicit InGameDialog(Surface surface = Surface::Match);

  protected:
	// Available content bounds with a visible 16-point gutter outside the panel.
	GAGGUI::ui::Rect insetAvailable(const Presentation &p, const GAGGUI::ui::Metrics &m);
	bool scrim() const override { return false; }
	// Action row: content-sized buttons at the right on pointer hosts, the touch
	// action grid otherwise.
	Element dialogActions(std::vector<MenuAction> items, const Presentation &p) const;
};

// Typography conventions shared by every screen (see docs/development/ui-framework.md,
// "Typography"): page titles are Heading and left-aligned; Title is kept for hero
// content (the wordmark fallback, the result banner, big numbers); section headers
// use heading(); body text, lists, fields and action buttons use Body; hints and
// help use hint(); small metadata uses caption().
Element pageTitle(const std::string &text);
// Explanatory text under a control or section: Support size, muted ink.
Element hint(const std::string &text);
// Title above a list of actions. Pointer hosts get a narrow panel of 300-point
// body-font buttons with the escape action pinned at the bottom; touch hosts get
// a scrolling grid of large actions.
Element menu(const std::string &titleText, std::vector<MenuAction> actions, const Presentation &p);
// Action row of content-sized body-font buttons at the right; on touch hosts it
// becomes a wrapping grid of large buttons.
Element actions(std::vector<MenuAction> actions, const Presentation &p);
// Dialog page: title, body and actions in the centered 640x480 paper
// panel on pointer hosts; a content-sized card within the safe area on touch.
Element page(const std::string &titleText, Element body, Element actionRow, const Presentation &p,
			 double maxWidthPoints = 640);
// Sprite frames cycled on wall time.
Element animation(const std::string &key, GAGCore::Sprite *sprite, int frames, int millisecondsPerFrame);
// Hosts a legacy MapPreview widget (owned by the screen) at `points` square, with
// drag, wheel zoom and retry forwarded to it.
Element mapPreview(const std::string &key, ::MapPreview &preview, double points, bool flexible = false);
} // namespace Glob2UI
