# Front-end visual revamp

This directory holds the review that motivated the revamp and the captures that
show its result. The review's measurements stand as written; the "spike" it
described has been replaced by the changes summarised below.

Captures come from the real client renderer via `MenuColonyHarness capture`,
at 1152x720. `before-*.png` is the front end as it shipped after #202, #236 and
#237; `after-*.png` is this branch on the software backend; `after-gl-*.png` is
this branch on OpenGL, where cloud shadows, the camera drift and sub-tile
motion exist.

## What was wrong

The refreshed front end read as sterile because every channel that could carry
character was off at once:

- **Colour.** The chrome ran 11–38% saturation over a world at 56–75%, in a
  gray-cream nothing in the game uses, with a cream veil desaturating the
  colony so the pale interface could sit on it.
- **Line.** Every game sprite and every concept drawing in
  `datasrc/gfx/concept-art/` is a heavy, slightly wobbly ink contour. The
  interface separated surfaces by 4%-lightness steps and a 1 px hairline.
- **Life.** The "live colony" had a fixed camera, its cloud shadows never
  reached the panel, and the panel was 97% opaque: a wallpaper.
- **Structure.** Nine near-cream fills across five drifted palette copies, and
  three parallel drawing layers (theme hooks, `LobbyControls`,
  `SettingsScreenLayout`) — a full rewrite of the theme left the Settings
  panel pixel-identical.

## What changed

Six commits, each verified with the harness gates and captures.

1. **One palette.** `FrontendPalette` in `src/FrontendTheme.h`: ink, muted,
   gold, violet (the water's hue, for keyboard focus), membrane, gel, scrim.
   Every front-end literal routes through it.
2. **Inked gel on a membrane.** `FrontendTheme::blob()` draws a rounded
   contour with per-corner radii and a gentle, rect-seeded wobble as a 2 px ink
   ring around a fill with a gel highlight; `ring()` is the outline alone, for
   frames drawn over list contents. Theme hooks, the main menu, the lobby's pad
   helpers and the settings screen all draw through it. Panels are a warm,
   translucent membrane the colony tints through (opaque under low-speed
   graphics). Widget geometry and hit rects are unchanged.
3. **The world acts on the UI.** A `Style::afterPaint` hook runs after every
   widget has painted; the theme uses it for one cloud shadow+layer pass over
   the whole window, anchored to the colony's viewport, replacing the pass
   `drawMap` used to make under the panel (`DRAW_NO_CLOUDS`). The colony camera
   drifts about a tile and a half on a slow Lissajous. Both OpenGL-only by the
   engine's existing guards; both presentation-only, outside the simulation.
4. **Jelly.** Buttons swell up to 2 px under the cursor and squash 1 px while
   pressed; the front page fades in over its first ten frames.
5. **A glob on the front page.** The colony's worker sprite, in its team
   colour, walks the band under the utility buttons. The classic sheet's faint
   matte, invisible on grass, is thresholded off once at composite time.

## Backend notes

- Software: everything except cloud shadows over the UI, camera drift and the
  entry fade's sub-tile motion. The membrane, ink, jelly and glob all render.
- OpenGL: all of it. Cloud strength over the sheet follows the same
  `cloudMaxAlpha` setting as the world; if a maintainer finds it too strong on
  the large Settings sheet, that is one constant in `FrontendTheme::afterPaint`.
- Modal dialogs composite over a frozen snapshot of the parent and refill
  opaquely each frame (`OverlayScreen::executeModal`), so they stay opaque by
  design.

## Verification

Run from the repository root, `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`
unless noted:

```sh
scons -j8 release=0 server=0 build/src/glob2 menu-colony-harness custom-setup-test speed-tests
build/src/MenuColonyHarness check data/menu/colony.bin      # presentation, text bounds, checksums, routes
build/src/MenuColonyHarness navigation unused              # enter/cancel 14 real screen loops
build/src/MenuColonyHarness sessions unused                # play/quit, replay, results, editor
build/src/CustomGameSetupHarness                           # lobby model
xvfb-run -a build/src/CustomGameSetupHarness artifacts/ci   # lobby visual (needs an X server)
xvfb-run -a python3 test/run-game-speed-tests.py            # sim checksums unchanged (needs a window)
```

All pass on this branch. `MenuColonyHarness` previews now invoke the style's
`afterPaint`, mirroring `Screen::dispatchPaint`, so captures show what the game
shows; a 120-frame OpenGL `record` measured the world shifting (−9, −4) px over
80 frames and the panel band's luminance moving 198→204 as a shadow crossed it.

Per `AGENTS.md`, a maintainer playing this in motion is part of review. The
captures are evidence, not a substitute.
