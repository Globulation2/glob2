# Mobile UX team review evidence

Final combined source: `27cbb03c485f7328b7803294ca21ef5a2fce19a9`. Master fetched before final validation: `fe33142db145d3025bc28ea6c1ee52a3d96511eb`.

All 26 selected native checks pass on this final source, with no skips. This validates the combined stack; it is not a claim that every intermediate PR head received the full suite separately. The eleven draft PRs remain unmerged. Merge in the order below; each PR targets the preceding feature branch so its diff stays focused.

## Reviewed changes

Three reviewers covered gestures/timing, inspection/navigation, and layouts/allocation, followed by cross-review and integrated validation. Review fixes include semantic double-tap events with separate gameplay/editor policy, removal of the unused simulation timestamp parameter, shared ratio commits, shared read-only inspector lifecycle/close geometry, explicit toolbox/replay navigation precedence, direct resource priority over unit halos, fixed-footer classic Teams scrolling, and shared HUD/dial measurement with complete action-fit checking. Compact row fallback preserves the requested title placement and reachable confirmation actions. Comments explain coordinate, timing, lifecycle and cancellation contracts; maintained mobile and preview guides describe the final behavior.

| PR | Change | Reviewed head | Base branch |
| --- | --- | --- | --- |
| [#583](https://github.com/Globulation2/glob2/pull/583) | Fix landscape picker scrolling over map previews | `1028826bcdb65befb68dd967392c7f73afae7922` | `master` |
| [#584](https://github.com/Globulation2/glob2/pull/584) | Zoom in at the tapped spot on gameplay double tap | `3026bde7eb583765ed3f53f0c1a461b0ac5c20d6` | `codex/landscape-picker-scroll` |
| [#592](https://github.com/Globulation2/glob2/pull/592) | Improve thumb dial reach and unify swarm production proportions | `8ac0121ead2fa391eeb28b443e05d53abbe2401f` | `codex/double-tap-zoom-in` |
| [#593](https://github.com/Globulation2/glob2/pull/593) | Fix touch momentum after returning from another app | `390e56cf5d4355fae3731cbaf4d9ce869262156f` | `codex/radial-allocation-usability` |
| [#594](https://github.com/Globulation2/glob2/pull/594) | Add a dead zone before gameplay touch momentum can start | `4c1889d9ff97b108135325f7239bd074e8c1e3d4` | `codex/android-resume-momentum` |
| [#595](https://github.com/Globulation2/glob2/pull/595) | Move compact gameplay toolboxes opposite the selected thumb | `6612b69b710c67917370c6803791f40b1a14878f` | `codex/touch-momentum-dead-zone` |
| [#596](https://github.com/Globulation2/glob2/pull/596) | Inset Objectives and Teams dialogs and size short pages to content | `a3d8f2a88135ae9ba699ef59c9c3ce691fded185` | `codex/opposite-side-toolboxes` |
| [#597](https://github.com/Globulation2/glob2/pull/597) | Show resource information instead of the alternate Tools panel on touch | `15bde2c8dc2ee703c16ae74c800ca9d14691afe3` | `codex/inset-gameplay-dialogs` |
| [#598](https://github.com/Globulation2/glob2/pull/598) | Consistently dismiss transient panels on blank-map taps | `dcd87e35f08dec0ce20fedd44999576de36c2d2f` | `codex/touch-resource-info` |
| [#600](https://github.com/Globulation2/glob2/pull/600) | Add forgiving touch unit selection and a unit stats panel | `1a96c70b1034093be18a278e638b355826904f2d` | `codex/blank-map-dismissal` |
| [#602](https://github.com/Globulation2/glob2/pull/602) | Place the mobile building title below stats beside the minimap | `27cbb03c485f7328b7803294ca21ef5a2fce19a9` | `codex/touch-unit-inspector` |

## Verification

Native Linux release engine and unit harnesses, GCC 15.2 / SDL 3.4.16, portable GPU. Build: `GLOB2_SDL3_PREFIX=<native SDL SDK> CCACHE=1 scons -j16 release=1 server=0 engine-tests unit-tests`; see [build log](build.log). Working tree was clean; `git diff --check origin/master` passed.

Display checks use a separate Xvfb display per test group, with `SDL_VIDEO_DRIVER=x11` and `WAYLAND_DISPLAY` unset. Headless checks leave both SDL video-driver variables unset so the runner supplies its dummy driver. This avoids the host's Wayland auto-selection and prevents the X11 override leaking into headless viewport checks. Exact commands are attached. Fullscreen coverage is enabled for presentation sweeps.

| Group | Cases passed | Evidence |
| --- | ---: | --- |
| gameplay | 10 | [JUnit](gameplay.xml), [log](gameplay.log), [command](gameplay-command.txt) |
| headless | 10 | [JUnit](headless.xml), [log](headless.log), [command](headless-command.txt) |
| small-portrait | 1 | [JUnit](small-portrait.xml), [log](small-portrait.log), [command](small-portrait-command.txt) |
| small-landscape | 1 | [JUnit](small-landscape.xml), [log](small-landscape.log), [command](small-landscape-command.txt) |
| tablet-portrait | 1 | [JUnit](tablet-portrait.xml), [log](tablet-portrait.log), [command](tablet-portrait-command.txt) |
| tablet-landscape | 1 | [JUnit](tablet-landscape.xml), [log](tablet-landscape.log), [command](tablet-landscape-command.txt) |
| laptop | 1 | [JUnit](laptop.xml), [log](laptop.log), [command](laptop-command.txt) |
| fullhd | 1 | [JUnit](fullhd.xml), [log](fullhd.log), [command](fullhd-command.txt) |

The gameplay group covers five GameGUITouch cases, resume momentum, native map preview interaction, landscape list scrolling/performance, and custom game screens. The headless group covers touch recognition/editor layout, serial/incremental sessions and preview geometry/database/codec behavior. Six presentation sweeps cover phone portrait/landscape, tablet portrait/landscape, laptop and full HD, across presentations and safe insets.

Real SDL input regressions cover stationary touches, deliberate flings, anchored double taps, unchanged editor reset, unit/resource selection and scrolling, fog and target precedence, blank-map dismissal, stale selection invalidation, read-only→building→close, explicit toolbar/replay stats navigation, mirrored toolbox placement, production-divider commit/cancel, dialog gutters/scrolling, title bounds and detached close targets, and short landscape confirmation reachability. They also check that read-only navigation emits no game orders.

[Capture provenance](capture-sources.json) maps every PNG to its final-source test run; matching test output logs include source/compiler provenance. Screenshots are current renders, not mockups. Original isolated before/after evidence remains linked from each PR.

## Screenshots

| Production proportions | Title beside minimap |
| --- | --- |
| ![Production](touch-proportions-portrait.png) | ![Landscape title](building-header-landscape.png) |

| Constrained viewport fallback | Inset objectives |
| --- | --- |
| ![Fallback](building-header-safe-fallback.png) | ![Objectives](inset-objectives-320x568.png) |

| Resource information | Unit information |
| --- | --- |
| ![Resource](resource-wood-portrait.png) | ![Unit](unit-portrait.png) |

## Limits and intended feel

Hosted CI status is separate and must be checked on each PR. Physical Android/iOS touch feel and actual app-switch/resume have not been exercised on a device; native SDL regressions validate the clock/input paths. No new simulation, save, replay or network format changes are intended. Cross-platform simulation checksums were not rerun for these presentation/input changes.

The changes intentionally alter double-tap behavior, fling activation, target forgiveness, toolbox reach and production allocation. A maintainer playing the result on a phone remains part of review; automated passes do not establish subjective usability or absence of every defect.
