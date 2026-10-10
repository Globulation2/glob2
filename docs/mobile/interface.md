# Responsive presentation policy

Layout selection, safe areas and frontend interaction rules shared by desktop, browser and mobile hosts.

## Presentation selection

The phone presentation shares simulation, game orders, settings persistence and
lobby setup with desktop. `InterfacePresentation.h` selects the presentation;
`GameGUITouch` owns gameplay gestures and phone panels. Menus and dialogs are
element trees on the declarative UI framework (see the
[UI framework guide](../development/ui-framework.md)), which adapts one build
path per screen to phones, tablets and desktops. Automatic presentation uses available logical space and touch capability
on every host. Settings offers Automatic, Compact and Spacious. Spacious requires
480 points of map width beside the panel and 480 points of usable height;
the panel is 288 points for touch and the existing 160 points for mouse controls.
Spacious falls back to Compact when it cannot fit. Safe areas and interface scale
participate in fit; the onscreen keyboard only reduces dialog space.
Touch-capable hosts use targets at least 48 points tall. Pointer availability
controls hover hints without switching layouts. Resize preserves the camera,
selection, tool, panel, scroll position and text while cancelling held input.

`GLOB2_MOBILE_UI=1` forces the compact presentation for development; `0` forces
legacy controls. `touch-auto` and `touch-spacious` exercise touch presentation
with Automatic and Spacious layout policies on a native development host. This is
the single development override; leave it unset for normal saved preferences and
host capabilities. Rendering backend and mouse
motion do not select a presentation. SDL, OpenGL/WebGL and software rendering
share UI transforms and clipping. Mouse clicks use the same controls as touch;
Tab and Enter navigate adapted forms, and Page Up/Down scroll gameplay panels.
Mobile settings omit desktop window sizes, renderer selection and OpenGL-only
options because the operating system manages the viewport and the mobile build
uses the portable renderer.


## Safe areas

Interactive content uses the host safe rectangle, including Android system bars
and display cutouts and iOS safe-area insets. Backgrounds may extend edge to edge.
The framework's `Presentation::safe` and `Presentation::dialog` rectangles,
resolved from `mobileDialogSafe`, bound menus, footers, scroll viewports and
popups; full window dimensions are only appropriate for backgrounds and
pointer-coordinate conversion. Gameplay reserves
system insets separately from keyboard occlusion so the camera stays stable.

Insets can change without a window resize. The screen stack refreshes host metrics,
cancels held input and invalidates layout when this happens. The UI presentation
harness injects host-point gutters through `mobileSafeInsetsForTesting` and checks
every screen and dialog fixture against them on phones, tablets and desktops.
Keep this override unset outside tests. Device checks
must also cover Android gesture/three-button navigation and rotation. While the
rail is open the HUD asks Android 10+ to exclude its rectangle from the system
Back gesture (`hostGestureExclusion`), resending only when the rectangle
changes; Android honours at most about 200 dp of exclusion per edge, so check
that a drag out of the rail places a building rather than navigating back.


## Frontend layout and interaction policy

`Presentation` (`libgag/include/ui/Presentation.h`) owns frontend device
classification: the width class is Compact below 600 points and Expanded from
960, `phone()` is a touch host whose short edge is below 600 points, and
keyboard occlusion reduces the `dialog` rectangle without changing
classification. This policy is separate from the compact gameplay HUD policy
and preserves keyboard settings on narrow desktop windows.

Every menu builds one element tree; the framework stacks form fields, folds
action rows into the scrolling body and wraps action grids on narrow viewports.
The measured geometry controls drawing, clipping and hit testing. Dialog text
is distinct from interactive fields; the Enter-bound action receives primary
emphasis rather than the first button in layout order (which may be Delete).
Settings scrolls its heading, category selector, fields, save status and
actions on phones; a separate Back control commits pending text through the
existing save path. Category IDs and building-default slots remain stable
across presentations.

Custom setup keeps its draft in `CustomGameScreen` across its Map, Players and
Rules tabs on every size; there are no separate phone subpages. Launch is
disabled until the preview represents the current validated revision; keyboard
launch follows the same guard. Additional Game Options remains
multiplayer-only.

The gameplay touch harness also exercises German and Japanese inspectors and
editor actions in small portrait and landscape views, including constrained
keyboard layouts. Its localized captures stay in the isolated test profile. The
UI presentation harness checks every screen and dialog at phone, tablet and
desktop viewports, with platform gutters, in touch and pointer presentations:
interactive elements inside the safe rectangle, minimum touch targets, no
overlapping controls, unique keys and valid focus after resize. The menu colony
harness's `navigation` mode drives every menu through the screen stack and back
out with Escape. Run these alongside the `GameGUITouch` and `Settings` suites and
the custom-setup harness after shared frontend changes. Preserve the previous
gallery directory as the visual baseline; feedback keys must never be
renumbered.

The capture run also creates `checkpoints/foundation/`, `checkpoints/settings/`
and `checkpoints/setup/` indexes using the same images and feedback IDs. The
single-player Setup checkpoint excludes multiplayer Additional Game Options.
Missing runtime translation keys fail the capture run. Register new keys in
`data/texts.keys.txt`, provide English fallback text, and keep every language
table structurally complete; run `python3 data/check_translations.py` as well.
Languages with pending translations remain marked incomplete and use the runtime
English fallback. Frontend touch text uses separate 16/14-point font aliases so
changes to menu readability do not alter in-game/editor metrics.

[Mobile index](README.md) · [Documentation index](../README.md).
