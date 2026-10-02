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

const Theme &frontendTheme();
// The dark purple in-match look of the touch HUD.
const Theme &inGameTheme();
// The classic navy in-match look with the sprite frame and gold buttons.
const Theme &classicInGameTheme();
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

// A modal over gameplay or the editor: the classic navy box with the sprite
// frame on pointer hosts, the touch HUD's purple sheet on touch hosts.
class InGameDialog : public UIDialog
{
  public:
	InGameDialog();
	// Classic desktop look: no titles, fixed narrow boxes, gold 40-point buttons.
	bool classic() const { return classicLook; }

  protected:
	bool scrim() const override { return false; }
	void paintPanel(GAGGUI::ui::Canvas &canvas, GAGGUI::ui::Rect panel) override;
	// A classic button: 300 points wide, 40 tall, in the menu font.
	Element classicButton(const std::string &key, const std::string &label, std::function<void()> action,
						  SDL_Keycode shortcut = SDLK_UNKNOWN, bool enabled = true, double widthPoints = 300) const;
	// Action row: 135-point classic buttons at the right on pointer hosts, the
	// touch action grid otherwise.
	Element dialogActions(std::vector<MenuAction> items, const Presentation &p) const;

  private:
	bool classicLook;
};

// Title above a list of actions. Pointer hosts get the classic narrow panel of
// 300-point menu-font buttons with the escape action pinned at the bottom;
// touch hosts get a scrolling grid of large actions.
Element menu(const std::string &titleText, std::vector<MenuAction> actions, const Presentation &p);
enum class ActionStyle
{
	// 180-point menu-font buttons at the right, as the classic dialogs had.
	Classic,
	// Content-sized body-font buttons at the right, as settings and the lobby had.
	Compact
};
// Action row; on touch hosts it becomes a wrapping grid of large buttons.
Element actions(std::vector<MenuAction> actions, const Presentation &p, ActionStyle style = ActionStyle::Classic);
// Classic dialog page: title, body and actions in the centered 640x480 paper
// panel on pointer hosts; a content-sized card within the safe area on touch.
Element page(const std::string &titleText, Element body, Element actionRow, const Presentation &p,
			 double maxWidthPoints = 640);
// Sprite frames cycled on wall time.
Element animation(const std::string &key, GAGCore::Sprite *sprite, int frames, int millisecondsPerFrame);
// Hosts a legacy MapPreview widget (owned by the screen) at `points` square, with
// drag, wheel zoom and retry forwarded to it.
Element mapPreview(const std::string &key, ::MapPreview &preview, double points, bool flexible = false);
} // namespace Glob2UI
