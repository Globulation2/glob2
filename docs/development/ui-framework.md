# Menus and dialogs: the declarative UI framework

Every frontend menu, in-game dialog and editor dialog is one element tree that
the screen rebuilds from its model. The framework in `libgag/include/ui/`
(namespace `GAGGUI::ui`) owns layout, phone/tablet/desktop adaptation, live
relayout on resize and rotation, input, focus, scrolling, popups and theming.
The game-side bindings in `src/ui/FrontendUI.h` (namespace `Glob2UI`) add the
two themes, translation and shared page builders. Screens contain a model and a
`build()` function, nothing else.

## Rules

1. **Build, do not lay out.** A screen implements
   `Element build(const Presentation &p)` and returns a tree of containers and
   controls from its current model. Never compute pixel positions in a screen;
   express structure (column, row, wrap, footer, scroll) and let the host
   measure and arrange it for the current viewport.
2. **Change the model, then `invalidate()`.** Callbacks mutate the model and
   call `invalidate()`; the host rebuilds on the next frame. Any method that
   changes what `build()` would return must invalidate, including ones reached
   from timers, network listeners and harness entry points. The host also
   rebuilds on its own after resize, rotation, inset, keyboard and scale changes.
3. **Give every interactive element a stable string key.** Keys retain focus,
   scroll offset, pressed state, popup state and text drafts across rebuilds,
   and harnesses drive screens through them (`host().bounds("start")`). Use
   slash-separated paths for repeated controls (`colony/2/team`,
   `map/list/1`). Duplicate keys are an error the presentation harness reports.
4. **One tree, adapted by the presentation.** Read `p.compact()`,
   `p.phone()`, `p.landscape()`, `p.shortLandscape()` and `p.touch` to choose
   structure inside `build()`, or use `adaptive()` to choose by the width the
   parent actually offers. Do not keep separate desktop and phone build paths
   for one screen; both must share the model and the keys.
5. **Sizes and text are points, resolved through `p.pt()`.** Theme metrics
   (control height, gap, padding, minimum touch target, dialog width) are host
   points; `p.pt(points)` converts to logical pixels for the current scale. Do
   not use raw pixel constants. The framework sizes text from the player's
   text size (in points on touch hosts), so a screen never scales fonts itself
   (see [Text size](#text-size)).
6. **Text wraps or ellipsizes; it is never clipped silently.** Use
   `paragraph()` for wrapping text and `label()` for single lines that
   ellipsize. Controls shrink or wrap their labels within the framework's
   minimum sizes.
7. **Actions go in a `footer()`.** The footer pins the action row to the
   bottom while it fits and folds it into the scrolling body when the viewport
   is too short (keyboard up, short landscape phone). The Escape route is the
   last action and carries `SDLK_ESCAPE`; the primary action carries
   `SDLK_RETURN` where a form has one.
8. **Screens never see coordinates.** Only the host converts SDL events. Gameplay
   forwards already translated events to `UIDialog::eventLogical`; standalone
   dialog callers use `event` for raw window coordinates. A
   screen that must react to a raw event overrides `interceptEvent()` (consume
   before the host) or `onEvent()` (after), and does so for keys, not
   positions. Custom painting goes through `canvas()`, which hands the painter
   the arranged rectangle.
   Batch native events with `GAGCore::EventQueue`, which owns SDL3 text-input
   and composition strings. Use it also when deferring input to a gameplay
   tick; SDL's temporary text pointers must not survive another event pump.
9. **Dialogs are hosted, never looped.** A `UIDialog` is driven by its owner
   (`GameGUI`, `MapEdit`, `EndGameScreen`, a parent screen) through
   `event()`, `update()`, `draw()` and `finished()/result()`.
   Mouse events passed to `event()` already use the owner's logical
   coordinates; its input boundary converts SDL window coordinates once.
   There is no nested event loop and no captured background; the scrim and panel are
   painted onto the owner's surface each frame.
10. **Keep harness entry points semantic.** Expose what a test needs as a
    method on the screen (`selectTab`, `setServer`, `confirm`) rather than
    giving tests widget internals. Screens may name harness structs as
    friends for state inspection.
11. **Keep the established look.** The builders above already reproduce the
    former desktop and phone layouts. Before changing how a screen looks,
    compare it against the previous release with the gallery
    (`tools/mobile_gallery/capture.py`) at a desktop and a phone size; the
    framework is a means of keeping screens responsive, not a redesign.
12. **Register new text keys** in `data/texts.keys.txt` with English fallback
    text in `data/texts.en.txt`; the capture runs fail on missing keys.
    Leave untranslated catalog values blank so the runtime uses English, and mark
    those catalogs with `*` in `data/texts.incomplete.txt`. When English fallback
    is accepted for new controls, list only those keys in `data/texts.pending.txt`;
    strict validation still rejects other untranslated text and all structural
    errors. Remove pending keys once every catalog has a translation.

## Anatomy of a screen

```cpp
class LANFindScreen : public Glob2UI::Screen
{
  public:
	explicit LANFindScreen(GAGGUI::ScreenStack &screens);
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void setServer(const std::string &address); // harness entry point
	void connect();

  protected:
	void onEscape() override { endExecute(QUIT); }

  private:
	std::string serverName = "localhost", playerName;
	std::vector<std::string> games;
	int selectedGame = -1;
};

Element LANFindScreen::build(const Presentation &p)
{
	auto fields = form({
		field(tr("[svr hostname]"), textField("server", serverName, [this](const std::string &v) { serverName = v; })),
		field(tr("[player name]"), textField("player", playerName, [this](const std::string &v) { playerName = v; })),
	});
	auto list = listView("games", games, selectedGame, [this](int i) { selectedGame = i; invalidate(); });
	return page(tr("[find LAN games]"), column({fields, expanded(list)}, {p.pt(8)}),
				actions({{"connect", tr("[connect]"), [this] { connect(); }, true, SDLK_RETURN},
						 {"back", tr("[goto main menu]"), [this] { endExecute(QUIT); }, false, SDLK_ESCAPE}},
						p),
				p);
}
```

`Glob2UI::Screen` paints the live colony background and keeps the frontend theme
active for its lifetime. `Glob2UI::Dialog`
is the same for frontend modals; `Glob2UI::InGameDialog` uses `inGameTheme()`,
the frontend's paper look, for gameplay and editor dialogs on every host. On big
desktop windows whose interface scale follows a 100 % desktop, `Glob2UI::Screen`
enlarges points and text up to 1.5x (`ui::comfortScale`, via
`UIScreen::adjustPresentation`); gameplay and dialogs keep their sizes. `endExecute(code)` and the
`ScreenStack` completion callback remain the navigation contract; push child
screens onto the stack rather than running them inline.

### Presentation

`Presentation` is resolved once per frame from the graphic context and passed
to `build()`:

| Member | Meaning |
| --- | --- |
| `viewport`, `safe`, `dialog` | Whole surface; minus platform gutters; minus the onscreen keyboard. Content goes in `safe`, forms and dialogs in `dialog`. |
| `unit`, `pt()` | Logical pixels per point and the conversion. |
| `textScale` | Text enlargement over the fonts' authored size: the player's text size, times the theme's `touchTextScale` on touch hosts. |
| `textGrowth`, `textPt()` | The player's text size alone (1 at 100%), and a length that grows with it, for widths that hold text such as a label column. |
| `textUnit` | Logical pixels per authored font pixel, used by the text measurer and canvas: `unit × textScale` on touch hosts, `textScale` on pointer hosts. |
| `touch`, `hover` | Touch capability (48-point targets) and pointer availability (tooltips). |
| `compact()`, `expanded()` | Width class thresholds at 600 and 960 points. |
| `phone()`, `landscape()`, `shortLandscape()` | Device shape helpers for structural choices. |

### Text size

Fonts are rasterized at authored pixel sizes, while the logical surface differs
between screens: gameplay and the editor keep an 800x600 minimum, so on a phone
their `unit` is 1.5–2 while menus run at 1. Touch text is therefore sized in
points, like every other metric: the framework measures and draws it at
`p.textUnit`, and glyphs are rasterized at the size they are drawn
(`TrueTypeFont::updateRenderScale`). One text size then holds on every touch
surface. Pointer hosts keep the authored pixel sizes times the preference, so
desktop layouts are unchanged at 100% (`applyTextSize()` holds the rule).

- The player's preference (`Settings::textSizePercent`, 100–150%, in Settings
  > Display and the in-game options) sets `GAGCore::userTextScale` through
  `Settings::setTextSizePercent()`. Every host picks it up on its next frame and
  relayouts; screens need no code for it.
- Themes carry the touch base (`Theme::touchTextScale`, 1.15 for the game's
  themes), so menus, dialogs over gameplay and the end-of-game sheet match.
- Bespoke touch painters (the gameplay HUD, the phone editor) draw text at
  `gfx->textUnitsPerPoint()` times a role factor, never at
  `logicalUnitsPerPoint()`, and grow rows that are sized by their text with
  `InGameTouchTheme::textGrowth()`.
- A control's measured size must hold the text it paints at every text size.
  `test/UILayoutHarness.cpp` and the presentation harness check that no
  painted line crosses a control's edge, at 100% and 150%.

### Containers and controls

Containers: `column`, `row`, `stack`, `wrap` (grid by minimum child width),
`scroll` (vertical, owns wheel, pan, scrollbar and scroll-into-view),
`adaptive`, `footer`, `card`, `form`/`field` (label beside the control when
wide, stacked when narrow), `padding`/`pad`, `align`/`center`, `sized`,
`width`, `height`, `maxWidth`, `expanded` (flex), `spacer`, `divider`.

Controls: `label`, `heading`, `title`, `caption`, `paragraph`, `button`,
`toggle`, `choice` (popup dropdown), `chooser` (opens a child screen),
`segments`, `stepper`, `slider`, `textField` (single line, IME composition,
browser text input), `textEditor` (multi-line), `listView` (virtualized rows
with selection and activation), `progress`, `image`, `sprite`, `swatch`,
`icon`, `canvas` (custom painter with pointer and wheel callbacks).

Game-side builders in `Glob2UI` reproduce the game's established look on
each host, so a screen written with them looks as it did before the
framework:

- `page()`: on pointer hosts the classic centered 640x480 paper panel with
  the title in the menu font, a scrolling body and 180-point menu-font
  buttons at the bottom right; on touch hosts a content-sized card with a
  folding footer.
- `menu()`: on pointer hosts the narrow panel of 300-point buttons with the
  Escape action pinned at the bottom; on touch hosts a grid of large actions.
- `actions(items, p, style)`: `ActionStyle::Classic` gives the 180-point
  menu-font buttons (wrapping into a grid when they cannot fit);
  `ActionStyle::Compact` gives content-sized body-font buttons for the
  screens that always had small ones (settings, the lobby).
- `animation()` and `mapPreview()` (hosts a `MapPreview` in a canvas).

### Theme

`Theme` carries a palette (ink, muted, paper, panel, field, rail, line,
accent, selected, hover, focus, scrim, disabled, danger, success, shadow,
pressed, backdrop, neutral, placeholder), font roles (Title, Heading, Body, Support,
Caption mapped to toolkit font names) and metrics in points. `frontendTheme()`
is the paper look for menus, and `inGameTheme()` the same look for in-game
dialogs and the results screen, so a match's menus read like the Online hub and
the room. `classicInGameTheme()` (the former navy box with the sprite frame and
gold 40-point buttons) is no longer used by `InGameDialog`; `classic()` is
always false, and the `classicButton()` branches in dialogs are kept only until
they are removed. Controls take colors and sizes from the theme only; a screen
that needs a color for data (a team swatch) passes it to `swatch()` or
`TextOptions::color`. Every colour outside gameplay's own HUD palette comes
from one of the three themes: the old `FrontendTheme` style that paints the
colony backdrop and paper panel reads the frontend palette rather than
keeping its own, and screens reach the palette through `theme().palette`.
The only literals left are data painters (map preview frames, the end-game
chart) that draw values rather than controls.

### Icons

Common interface icons use Tabler outlines, coloured with the active theme. Request
assets with `Glob2UI::uiIcon(UIIcon::Settings)` (or another semantic name); screens
must not load filenames. `icon(asset, {20})` creates a decorative element whose size
is in points. `ButtonOptions::icon` adds an icon before the button label with a
6-point gap; `iconSize` defaults to 20 points. The icon takes the button's resolved
ink colour, including disabled, primary and danger states.

For an icon-only button, pass empty visible text and a translated
`accessibleLabel`; unnamed icon-only buttons are rejected. Set `tooltip` to the
translated label. The host displays it after 600 ms of pointer hover or keyboard
focus, keeps it inside the safe viewport, and dismisses it on activation or target
change. Touch actions retain their normal minimum target size. A missing asset
restores the accessible label as visible button text; missing raster files are
logged when the asset is first requested. These names support diagnostics and
harnesses; they do not establish native screen-reader integration.

`GAGGUI::ui::IconAsset` owns white alpha-mask raster variants sorted by width. The
canvas chooses the smallest adequate resolution and caches recolours, preserving
coverage without changing the source mask. Recording canvases record `drawIcon`
identity, bounds and colour without accessing pixels. Game-side lookup uses a weak
cache keyed by asset name: live elements own their masks and recolours, aliases
share them, and closing the last element releases the graphics resources.

The main-menu Settings button uses a gear with its label in the desktop utility
grid, and a gear alone beside the wordmark on touch hosts. Desktop Editor and Load
game also retain their labels beside icons. Touch main-menu launch choices and
More-page actions all use icons beside labels, including Back and More. Desktop
settings-sidebar categories use labelled icons; the compact category dropdown
and game-specific artwork retain their existing presentation.

Choose icon-only controls for familiar actions in headers and toolbars. Keep
labels in menu rows and desktop utility grids so neighbouring controls read
consistently. Mobile interfaces can use icons more extensively to aid scanning,
while retaining labels for destinations that need explanation.

`Glob2UI::compactButton` applies that toolbar convention: translated text on
pointer hosts, a 24-point icon in a button at least 48 points square on touch
hosts, with a diagnostic name, tooltip and visible-text fallback. Mobile chat
uses Send and Close icons, the compact landscape picker uses a Back arrow, and
custom-game previews use Refresh and Info icons for reroll and start quality.
Launch/confirmation choices, parameter operations and immediate file deletion
keep their labels. Landscape parameter fields widen with mobile text size so
numeric choices remain readable. The gameplay action strip retains its
game-specific sprites and labels, which distinguish construction, flags and team tools.

Original SVGs and a source manifest live in `datasrc/icons/tabler/`, pinned to
Tabler v3.48.0. Generated PNGs live in `data/gui/`; the distributed MIT notice is
`data/tabler-icons-license.txt`. Ordinary builds use the committed PNGs and need
no SVG renderer or network access. To regenerate with the pinned development tool:

```sh
npm install --prefix artifacts/tabler/tooling --no-audit --no-fund @resvg/resvg-js@2.6.2
NODE_PATH=artifacts/tabler/tooling/node_modules node tools/icons/export_tabler.cjs
```

The exporter verifies source hashes and writes 20- and 24-point icons at 1×, 2×
and 3×. Add new assets deliberately to the manifest and semantic bindings, retain
upstream notices, and capture desktop and phone views when introducing them.

### Hosting a dialog

```cpp
std::unique_ptr<Glob2UI::InGameDialog> dialog = std::make_unique<InGameOptionScreen>(...);
dialog->attach(*globalContainer->gfx);          // once, on open
// each frame
if (dialog->event(event)) return;               // consumed
dialog->update(SDL_GetTicks());
dialog->draw(SDL_GetTicks());
if (dialog->finished()) act(dialog->result());
```

Dialogs override `onEscape()`, `available()`/`place()` for non-centered
placement (the chat composer sits at the bottom), `maxWidth()`, `fillHeight()`
and `scrim()`. `resume()` reopens a dialog whose result was consumed but must
retry (a failed save).

### Focus, keyboard and touch

The host keeps one focus ring over interactive elements in tree order. Tab and
Shift+Tab move focus, arrows move within lists, sliders, segments and popups,
Enter and Space activate, Escape ends text editing, closes a popup or fires the
screen's Escape route. Shortcut keys on buttons fire without focus. Touch
input distinguishes taps from pans; a pan scrolls the innermost `scroll` and a
release outside the pressed element cancels it. Pointer capture guarantees
press and release land on the same element.

Finger pans on `scroll`, `listView` and `textEditor` (the nodes whose
`inertial()` is true) have the physics of a native list, driven by
`GAGCore::ScrollAxis` from `libgag/include/ScrollPhysics.h`:

- the content tracks the finger, and after release coasts with the finger's
  velocity (a least-squares fit over the last 100 ms; a finger that rests for
  40 ms before lifting has none) and decelerates at the iOS rate;
- dragging past either end stretches the content with the iOS rubber band and
  releasing it springs back with a critically damped spring; a coast that
  reaches an end hands its velocity to the same spring;
- a touch on coasting content stops it where it is and is not a tap; the
  wheel, a scrollbar press, `scrollIntoView`, `scrollToEnd`, a rebuild that
  moves the content, focus loss and a resize all end the motion.

Nodes keep an `int` offset that is always clamped, which is what
`NodeState::scroll` persists, plus a transient `overscroll()` displacement that
is painted and hit-tested but never saved; `scrollTo()` sets the clamped offset
and `setOverscroll()` the stretch. Mouse drags never coast or stretch, so
desktop behaviour is unchanged. `Host::animating()` reports a coast or bounce
in progress and `UIScreen::executionDelay` frames every 16 ms meanwhile.
The three preference sliders under Settings › Controls (and the in-game
Options dialog on touch) tune the feel through `GAGCore::scrollTuning()`; 0
turns momentum or bounce off, 50 is the researched default.

## Verification

- `scons unit-tests` then `python3 test/run_tests.py --binary unit --filter 'UILayout/*'`
  runs `test/UILayoutHarness.cpp`: pure layout with fixed-advance text and a
  recording canvas. Measure and arrange,
  adaptive re-choice after resize, footer folding, scroll clamping, wrapping,
  ellipsis, focus order, capture, tap versus pan, fling, overscroll and bounce
  (with an explicit clock: event timestamps and `host.update(tick)`), popup
  routing, per-key state across rebuilds and text editing. The `ScrollPhysics`
  suite covers the kernel itself.
- The `UIPresentation` suite in `glob2-engine-tests`
  (`python3 test/run_tests.py --filter 'UIPresentation/*'`) instantiates every
  screen and dialog fixture at phone, tablet and desktop viewports in both touch
  and pointer presentations, with and without platform gutters, and requires:
  every interactive element inside the safe rectangle, touch targets at least
  the theme minimum, no overlapping interactive elements, unique keys, something
  focusable, and a valid focus and scroll state after resize. It writes one
  image per screen and viewport into its artifact directory.
  `GLOB2_UI_ONLY=<fixture>` restricts a run. `GLOB2_UI_VIEWPORT=<viewport>`
  selects a named viewport, and `GLOB2_UI_REVEAL=<control-key>` scrolls a control
  into view before capture and layout verification.
- The `GameGUITouch`, `Settings`, `EngineSession`, `GameSpeed`,
  `CustomGameSetup` and `MapPreview` suites, `menu-colony-harness` and
  `mobile-gallery` drive the ported screens through their keys and semantic
  entry points; run them after changing a shared builder or the host.
- `GLOB2_UI_DEBUG=1` draws element bounds and keys over any screen.
- Feel is not covered by tests: resize the desktop window live through the
  menus, rotate with `GLOB2_MOBILE_UI=1`, and play a session before merging a
  change to a shared builder or theme.

## Adding a screen

1. Derive from `Glob2UI::Screen` (menu), `Glob2UI::Dialog` (frontend modal)
   or `Glob2UI::InGameDialog` (gameplay/editor modal).
2. Keep the model as plain members; write `build()` with the containers above;
   give each control a key and a callback that changes the model and
   invalidates.
3. Add the screen to the presentation harness fixtures in
   `test/UIPresentationHarness.cpp` and, for a reviewable capture, to
   `tools/MobileGalleryHarness.cpp` and `tools/mobile_gallery/catalog.json`.
4. Expose semantic entry points for interaction tests instead of coordinates.
