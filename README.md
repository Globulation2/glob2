# Evidence: Custom Game screen icons

Tested commit `d820dda10` on base `dcc3aba5a` (merges cleanly with `a6812d894`).
Linux x86_64, GCC 15, SCons `release=1 server=0`, pinned SDL3 prefix.

- `compare-*.png` — before (HEAD build of `dcc3aba5a`) vs after, `MenuColonyHarness capture`.
- `before/`, `after/` — the raw captures.
- `touch/` — touch-presentation captures from the UIPresentation suite, including
  `small-portrait-sheet.png` (320 px phone, normal and 150 % text: row icons drop out).
- `logs/` — UIPresentation + custom game screens (7/7), UILayout (27/27), icon asset tests,
  and the phone `MenuColonyHarness` "control within screen" checks, which fail
  identically before and after because those tabs scroll.
