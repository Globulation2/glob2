# Front-end visual review

A review of the visual result of the three recent front-end changes — the menu
refresh (#202), the Settings redesign (#236) and the custom-game lobby (#237) —
after feedback that the new interface reads as sterile. It is a diagnosis with
supporting measurements and one demonstration spike. **Nothing here is proposed
for merge.**

Captures come from the real client renderer via `MenuColonyHarness capture`,
at 1152x720 with the software backend.

## The short version

The old look was not liked because of *how it was executed* — a seamless grass
texture tiled at 1:1, and bevel-embossed gold gradients. But it did three things
the new one does not: it used the game's own colours, its controls read as
physical objects, and menu and match looked like one product.

The refresh removed the bad execution and the three good properties with it.
Flat design puts the whole burden of personality on palette, shape and type. The
current front end is flat *and* uses a palette taken from nothing in the game
*and* draws no outlines *and* uses the stock system font *and* shows none of the
game's art. Every channel that could carry character is switched off at once.

The fix is not to restore texture. It is to switch two or three of those
channels back on.

## What the measurements say

### The chrome is not made of the game's colours

Dominant colours, sampled from `data/gfx/menu-colony.png` (the world behind the
menu) and from the theme's own constants:

| | hue | saturation | lightness |
| --- | --- | --- | --- |
| grass `rgb(16,96,16)` | 120° | **71%** | 22% |
| water `rgb(64,48,192)` | 247° | **60%** | 47% |
| beach `rgb(176,160,48)` | 52° | **57%** | 44% |
| panel `rgb(230,231,210)` | 63° | **30%** | 86% |
| button `rgb(234,240,228)` | 90° | **29%** | 92% |
| hover `rgb(169,196,157)` | 102° | **25%** | 69% |
| frame `rgb(137,160,132)` | 109° | **13%** | 57% |

The world runs 56–75% saturation. The chrome runs 11–38%, median 25%. The greens
do not even share a hue family: the game's green is a pure 120°, the chrome's is
a yellowed 102–109° sage. And violet — 22% of the world's pixels — appears
nowhere in the interface, so the game's actual signature contrast (vivid green
against deep violet) is thrown away.

`FrontendTheme::background` then draws `Color(232,237,218,42)` across the whole
window: a cream veil that desaturates the game's art so the pale interface can
sit on top of it. The art is being dimmed to accommodate the chrome.

### There is no ink line, and the game is made of ink lines

The recovered concept art in `datasrc/gfx/concept-art/` is unambiguous about the
house style: heavy, slightly wobbly hand-inked contours around soft bulbous
forms, often doubled with a parallel offset stroke. Every building and unit
sprite carries a dark outline.

The interface separates surfaces with 4%-lightness steps and a 1px pale-sage
`drawRect`. It is the one drawing convention the game's art never uses.

### Seven surfaces, one value, five palettes

Panel-ish fills currently in use: `(230,231,210)`, `(234,240,228)`,
`(202,218,196)`, `(243,245,233)`, `(222,226,212)`, `(232,237,218)`,
`(211,223,197)`, `(240,241,223)`, `(249,250,240)`. All between 76% and 94%
lightness, hues scattered across 63–104°. Too close to read as deliberate
levels, too different to look intentional.

They are spread over five independent definitions that have already drifted:

| | file |
| --- | --- |
| `FrontendTheme` | `src/FrontendTheme.cpp:19-23` |
| main menu | `src/MainMenuScreen.cpp:28-31` |
| lobby | `src/LobbyControls.h:36` |
| settings | `src/SettingsScreenLayout.cpp:9` |
| lobby screen | `src/CustomGameScreen.cpp:51,566,644,1043` |

Muted text is `(92,114,91)` in the main menu and `(89,108,86)` in the lobby.
Panel shadow is `(15,39,25,35)` in the theme and `(34,54,36,100)` in the lobby.
Nobody chose those differences.

### The newest screens bypass the theme

This is the structural finding, and it is measurable. The spike below rewrites
`FrontendTheme` end to end. Cropped to the Settings panel interior, the before
and after captures are **pixel-identical** — `ImageChops.difference(...).getbbox()`
returns `None`. The custom lobby picks the change up only partly, because half
its colours come from the theme and half are literals in its own file.

There are now three parallel drawing vocabularies in the front end:

1. `FrontendTheme`, through the GAG `Style` hooks — used by the older screens
2. `LobbyControls` — the lobby's own immediate-mode widget kit
3. `SettingsScreenLayout` — the settings screen's own immediate-mode kit

Each with its own palette, its own corner radii (theme rounds at r=4–10, the
lobby at r=5, settings draws square `drawRect`s) and its own focus treatment.
Any styling decision has to be made three times, which is a good part of why it
has not been made forcefully once.

### Composition

`Glob2Screen::drawFrontend` sizes the shared panel as the bounding box of every
visible widget, unioned with a 640x480 minimum. That is not a layout — it is a
beige rectangle stretched around whatever is on screen, which is why the results
screen is a full-bleed sheet with a hairline chart on it. The main menu panel is
separately pinned at `min(height-40, 620)` while its content ends around 500,
which is the empty band under the utility buttons.

### Two more

- **Type.** `data/fonts/sans.ttf` is DejaVu Sans, used at four sizes with no
  weight contrast. The wordmark is a rounded, playful face; the menu under it is
  a Linux system font. They do not look related.
- **Menu and match are different products.** `FrontendScope` suspends the theme
  for gameplay, so a match still renders the 2003 gold `Glob2Style` HUD. Whatever
  direction wins has to cover both, or the menus will keep feeling detached.

## The spike

To test whether the palette-and-outline reading is right, the branch carries an
experiment that changes *only* surface treatment — no layout, no fonts, no new
assets, about 40 lines:

- chrome recoloured to the game's own hues: parchment `(240,224,188)` from the
  beach sand, ink `(26,48,30)` from the grass, gold `(233,176,53)`, and the
  water's violet `(92,74,198)` as the focus ring
- a drawn 2–3px ink contour on panels, buttons, fields, sliders and scrollbars
- the cream veil over the world replaced with a dark scrim, so the art keeps its
  saturation instead of being washed out

`ab-main.png` is the comparison; `now-*.png` and `spike-*.png` are the four
captured screens each way.

What it establishes:

- Where the theme owns drawing, restoring two channels is enough. The main menu
  reads as belonging to the game again with identical layout and type.
- Palette alone does not fix composition. The results screen gets warmer and its
  buttons become objects, but it is still a bounding-box sheet.
- Settings does not change at all, and the lobby changes inconsistently. That is
  the argument for the ordering below.

## Suggested order

1. **Collapse the five palettes into one set of named tokens** on the theme, and
   route `LobbyControls` and `SettingsScreenLayout` through it. Pure refactor, no
   visual change, and until it exists no visual direction can actually be applied
   or iterated.
2. **Recolour from the game's art** and **add the ink contour.** Cheapest change
   with the largest effect on whether it feels like Glob; the spike is a starting
   point, not a finished palette.
3. **Stop veiling the world.** Dark scrim rather than cream wash; consider
   letting the colony show through the panel rather than sealing it off.
4. **Compose the shared screens.** Replace the bounding-box panel with a real
   header/content/footer frame, and size the main-menu panel to its content.
5. **Use the game's own art in the interface.** 1450 sprites ship in `data/gfx/`
   at 32x32 (units) and 96x96 (buildings), plus the high-res pack, and not one
   appears in a menu. Building icons on the settings categories, a glob for the
   checkbox tick, unit sprites in the lobby colony rows.
6. **Give headings a face that matches the wordmark**, or at least real weight
   contrast.
7. **Decide what the in-game HUD does**, so a match and its menus look like one
   game.

Items 1–3 are mechanical and reversible. Items 4–7 are design decisions that
want a maintainer's eye on the result in motion, not just in screenshots.
