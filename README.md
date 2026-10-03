# Evidence: UI consistency pass (claude/ui-tweaks-consistency-1252ff)

Tested commit 409c262e8 on base 21e838582; native Linux x86_64 release build, Xvfb.

- `ui-*-laptop-touch0.png`: desktop captures from `test/UIPresentationHarness.cpp`
  (laptop viewport): main menu with icons, Settings > Hive Mind / Recording /
  Online, LAN join, Load game, Online hub, room.
- `menus.png`: LAN, Campaign and Editor menus (content-sized panels, body-font buttons).
- `dialogs.png`: editor menu, in-game menu, outcome and save dialogs in the
  in-match theme (`GUIInteractionCoverage/match and editor dialogs wear the in-match theme...`).
- `tests.log`: `run_tests.py -j 24 --quick` (562 passed, 4 failed).
- `base-failures.txt`: the same 4 cases fail on unmodified base 21e838582.
- `focus.log`: recording/settings/key-action/editor/touch cases with
  `GLOB2_TEST_FFMPEG=/usr/bin/ffmpeg` (all recording cases pass; the 3 failures
  are the base failures above).
- `dlg.log`: GUIInteractionCoverage suite (6 passed).
