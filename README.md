# Evidence: size UI text in points

Gallery captures from `tools/MobileGalleryHarness.cpp` (SDL software renderer,
dummy video driver, macOS), resized to device points so text compares at the size
a player sees. "Before" is master at af6754155; "after" is the text-size-points branch.

- `00-problem-before-fix.png` — the original problem: menus vs in-game options at
  100% and 150% on a landscape phone (menu unit 1.00, in-game unit 1.54).
- `phone-*-settings-vs-options.png`, `phone-*-load.png` — menus and in-game dialogs
  before/after at 100%: in-game text now matches menu text in both orientations.
- `phone-portrait-hud.png` — gameplay HUD at 100% is unchanged.
- `final-landscape-settings.png`, `final-portrait-150.png` — 150% text: settings
  falls back to the category dropdown, setup and in-game dialogs reflow with no overlap.
- `phone-landscape-settings-rail-100.png` — the settings rail at 100% overlapped
  ("player" under "Experiments") before; it now scrolls.
- `desktop-unchanged-settings.png` — desktop before | after | changed pixels: only
  the animated colony background differs.

Captures are not pixel-deterministic (the menu background animates on wall time), so
`panel_diff.py` compares only menu paper panels; a same-binary before/before run sets
the noise floor (0–70 px; landscape picker and credits are random/animated).
