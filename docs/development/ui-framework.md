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
5. **Sizes are points, resolved through `p.pt()`.** Theme metrics (control
   height, gap, padding, minimum touch target, dialog width) are host points;
   `p.pt(points)` converts to logical pixels for the current scale. Do not use
   raw pixel constants.
6. **Text wraps or ellipsizes; it is never clipped silently.** Use
   `paragraph()` for wrapping text and `label()` for single lines that
   ellipsize. Controls shrink or wrap their labels within the framework's
   minimum sizes.
7. **Actions go in a `footer()`.** The footer pins the action row to the
   bottom while it fits and folds it into the scrolling body when the viewport
   is too short (keyboard up, short landscape phone). The Escape route is the
   last action and carries `SDLK_ESCAPE`; the primary action carries
   `SDLK_RETURN` where a form has one.
8. **Screens never see coordinates.** Only the host converts SDL events. A
   screen that must react to a raw event overrides `interceptEvent()` (consume
   before the host) or `onEvent()` (after), and does so for keys, not
   positions. Custom painting goes through `canvas()`, which hands the painter
   the arranged rectangle.
9. **Dialogs are hosted, never looped.** A `UIDialog` is driven by its owner
   (`GameGUI`, `MapEdit`, `EndGameScreen`, a parent screen) through
   `event()`, `update()`, `draw()` and `finished()/result()`. There is no
   nested event loop and no captured background; the scrim and panel are
   painted onto the owner's surface each frame.
10. **Keep harness entry points semantic.** Expose what a test needs as a
    method on the screen (`selectTab`, `setServer`, `confirm`) rather than
    giving tests widget internals. Screens may name harness structs as
    friends for state inspection.
11. **Register new text keys** in `data/texts.keys.txt` with English fallback
    text in `data/texts.en.txt`; the capture runs fail on missing keys.

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

`Glob2UI::Screen` paints the live colony background, keeps the frontend theme
active for its lifetime and applies the user's text scale. `Glob2UI::Dialog`
is the same for frontend modals; `Glob2UI::InGameDialog` uses the dark in-match
theme for gameplay and editor dialogs. `endExecute(code)` and the
`ScreenStack` completion callback remain the navigation contract; push child
screens onto the stack rather than running them inline.

### Presentation

`Presentation` is resolved once per frame from the graphic context and passed
to `build()`:

| Member | Meaning |
| --- | --- |
| `viewport`, `safe`, `dialog` | Whole surface; minus platform gutters; minus the onscreen keyboard. Content goes in `safe`, forms and dialogs in `dialog`. |
| `unit`, `pt()` | Logical pixels per point and the conversion. |
| `textScale` | User text enlargement applied by the text measurer. |
| `touch`, `hover` | Touch capability (48-point targets) and pointer availability (tooltips). |
| `compact()`, `expanded()` | Width class thresholds at 600 and 960 points. |
| `phone()`, `landscape()`, `shortLandscape()` | Device shape helpers for structural choices. |

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
`canvas` (custom painter with pointer and wheel callbacks).

Game-side builders in `Glob2UI`: `menu()` (title over a scrolling grid of
large actions), `actions()` (wrapping action row), `page()` (title, body and
pinned actions in a centered card), `animation()`, `mapPreview()` (hosts a
`MapPreview` in a canvas).

### Theme

`Theme` carries a palette (ink, muted, paper, panel, field, accent, selected,
focus, scrim, disabled, danger), font roles (Title, Heading, Body, Support,
Caption mapped to toolkit font names) and metrics in points. `frontendTheme()`
is the paper look for menus; `inGameTheme()` is the dark in-match look used by
gameplay and editor dialogs and the end-game screen. Controls take colors and
sizes from the theme only; a screen that needs a color for data (a team
swatch) passes it to `swatch()` or `TextOptions::color`.

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

## Verification

- `scons ui-layout-test` builds `test/UILayoutHarness` (in `test/`): pure
  layout with fixed-advance text and a recording canvas. Measure and arrange,
  adaptive re-choice after resize, footer folding, scroll clamping, wrapping,
  ellipsis, focus order, capture, tap versus pan, popup routing, per-key state
  across rebuilds and text editing.
- `scons ui-presentation-test` builds `ui-presentation-test`, which
  instantiates every screen and dialog fixture at phone, tablet and desktop
  viewports in both touch and pointer presentations, with and without platform
  gutters, and requires: every interactive element inside the safe rectangle,
  touch targets at least the theme minimum, no overlapping interactive
  elements, unique keys, something focusable, and a valid focus and scroll
  state after resize. Run it with `capture` to write one image per screen and
  viewport under the working directory. `GLOB2_UI_ONLY=<fixture>` restricts a
  run.
- `gameplay-touch-test`, `custom-setup-test`, `settings-tests`,
  `session-test`, `map-preview-test`, `menu-colony-harness` and
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
