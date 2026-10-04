# Web artwork

The web app and the server-rendered sign-in and invite pages use the game's own
artwork, compressed for the web by `build_art.py`:

```sh
python3 platform/apps/web/art/build_art.py   # needs Pillow; run npm ci in platform/ first
```

It writes `platform/apps/web/src/art/` (imported by the app, so Vite fingerprints
the files) and `platform/apps/api/src/web/static/` (served by the API at
`/signin/assets/`). The outputs are committed; rerun the script after changing a
source.

| Output                                                                                                     | Source                                                                                                                                                                                                               |
| ---------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `colony-960.webp`, `colony-1600.webp`, `static/colony.webp`, `static/og-colony.jpg`                        | `data/gfx/menu-colony.png`, the main menu's colony                                                                                                                                                                   |
| `wordmark-letters.webp`, `wordmark-two.webp`                                                               | `data/gfx/menu-wordmark.png`, split into two alpha masks (letters, gold "2") so CSS can colour them per theme                                                                                                        |
| `worker-east.webp`, `worker-west.webp`, `warrior-east.webp`, `explorer-west.webp`                          | Unit walk and flight cycles: `data/highres/v1/unit<N>.png` (shadow) under `unit<N>r.png` (body), every other of the 32 poses, at 64 px. Frame layout from `src/unit/render/UnitSkin.cpp` and `src/unit/render/UnitAnimation.h` |
| `swarm`, `inn`, `school`, `racetrack`, `pool`, `hospital`, `exploration-flag`, `war-flag`, `clearing-flag` | Building and flag sprites (`data/highres/v1`, else `data/gfx`), with their team layer                                                                                                                                |
| `wood`, `fruit`, `papyrus`, `stone`, `algae`                                                               | Resource sprites `ressource4/14/24/38/44`                                                                                                                                                                            |
| `glob-64.png`, `public/favicon-32.png`, `public/apple-touch-icon.png`                                      | `data/icons/`, `mobile/ios/Assets.xcassets/AppIcon.appiconset/icon-180.png`                                                                                                                                          |
| `static/glob2-sans.woff2`, `public/fonts/glob2-sans.woff2`                                                 | Glob2 Sans, a Latin subset of `data/fonts/sans.ttf` (DejaVu Sans; licence in `public/fonts/LICENSE-DejaVu.txt`)                                                                                                      |
| `static/nunito.woff2`                                                                                      | Nunito, Latin, variable weight, from `@fontsource-variable/nunito` (SIL Open Font License 1.1)                                                                                                                       |

The game's artwork is part of Globulation 2 and licensed with it under the GPL 3
(see `docs/assets/source-attribution.md` and `data/authors.txt`). Engine-rendered
map previews used by the browser smoke test (`e2e/fixtures/maps/`) were made with
`glob2 --generate-map <generator> --preview <file> --preview-size 384 --teams N --seed 7`.

## Contrast

Text and control colours of both themes (`src/styles/tokens.css`) meet WCAG 2.2
AA; the browser smoke test runs axe on every page in both themes. Measured
ratios:

| Pair                                     | Meadow (light)  | Night colony (dark) |
| ---------------------------------------- | --------------- | ------------------- |
| ink on page / card                       | 9.5 / 10.4      | 14.8 / 12.8         |
| secondary ink on page / card / inset     | 6.0 / 6.6 / 5.7 | 10.1 / 8.7 / 7.9    |
| gold button text                         | 8.7             | 10.3                |
| gold text on page                        | 5.6             | 11.9                |
| focus ring on page                       | 4.8             | 13.0                |
| field and button borders on card / inset | 3.8 / 3.3       | 3.7 / 3.3           |
| danger / success / warning badges        | 5.7 / 5.1 / 5.8 | 6.4 / 7.7 / 7.7     |

Chart lines use team hues adjusted to at least 3.2:1 against the chart
background (`src/colors.ts`); swatches keep the exact in-game colour.
