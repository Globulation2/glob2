# Tabler icon validation

PR source: 89bf1dca772efde013e10e92f90d84e013d6503e

- macOS release client, unit-test and engine-test binaries built.
- All 29 related doctest cases passed: UILayout (17), DrawableSurfaceBlend (3), UIIcons (2), Settings (6), GameGUITouch (1). All cases passed in the final rerun on the PR head.
- Fresh presentation checks passed for mobile main menu, mobile More, desktop main menu, landscape navigation at 150% text and map reroll at 150% text, across their safe-area variants.
- Native software, portable and OpenGL paths covered by the renderer, settings and gameplay cases.
- All 21 pinned SVG hashes verified; regenerating 126 PNGs produced identical bytes.
- Installed all 126 rasters and the MIT notice; byte comparisons matched the sources.
- Strict translation catalog validation: zero structural errors.
- Final working-tree diff has no whitespace errors; no temporary evidence or tooling dependencies included in the source PR.

Source distribution limitation: after fixing single-node handling, the existing dist target still requests the removed README file. Full source-archive validation remains blocked by that pre-existing input list. Other platforms await hosted CI.

Screenshots are from this PR's current-master build. Ordinary builds need no SVG renderer or Node dependency.

Windows CI: failed at SavegameSafetyHarness.cpp:652 (unchanged by this PR); the exact namespace error is documented and fixed by existing PR #475. Job: https://github.com/Globulation2/glob2/actions/runs/36820798770/job/110236586009
