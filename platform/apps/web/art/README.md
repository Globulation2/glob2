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

| Output                                                                                                     | Source                                                                                                                                                                                                                         |
| ---------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `static/colony.webp`, `static/og-colony.jpg`                                                               | `data/gfx/menu-colony.png`, the main menu's colony                                                                                                                                                                             |
| `colony-loop.mp4`, `colony-960.webp`, `colony-1600.webp`                                                   | UI-free `MenuColonyHarness` recording of `data/menu/colony.bin` (see Colony video below)                                                                                                                                       |
| `wordmark-letters.webp`, `wordmark-two.webp`                                                               | `data/gfx/menu-wordmark.png`, split into two alpha masks (letters, gold "2") so CSS can colour them per theme                                                                                                                  |
| `worker-east.webp`, `worker-west.webp`, `warrior-east.webp`, `explorer-west.webp`                          | Unit walk and flight cycles: `data/highres/v1/unit<N>.png` (shadow) under `unit<N>r.png` (body), every other of the 32 poses, at 64 px. Frame layout from `src/unit/render/UnitSkin.cpp` and `src/unit/render/UnitAnimation.h` |
| `warrior.webp`, `worker.webp`                                                                              | Still poses for page art, cropped to the sprite: a warrior lunging (`unit<1536 + 4·32>`, attack, south-east) and a worker mid-stride (walk, east, pose 8)                                                                      |
| `swarm`, `inn`, `school`, `racetrack`, `pool`, `hospital`, `exploration-flag`, `war-flag`, `clearing-flag` | Building and flag sprites (`data/highres/v1`, else `data/gfx`), with their team layer                                                                                                                                          |
| `wood`, `fruit`, `papyrus`, `stone`, `algae`                                                               | Resource sprites `ressource4/14/24/38/44`                                                                                                                                                                                      |
| `glob-64.png`, `public/favicon-32.png`, `public/apple-touch-icon.png`                                      | `data/icons/`, `mobile/ios/Assets.xcassets/AppIcon.appiconset/icon-180.png`                                                                                                                                                    |
| `static/glob2-sans.woff2`, `public/fonts/glob2-sans.woff2`                                                 | Glob2 Sans, a Latin subset of `data/fonts/sans.ttf` (DejaVu Sans; licence in `public/fonts/LICENSE-DejaVu.txt`)                                                                                                                |
| `static/nunito.woff2`                                                                                      | Nunito, Latin, variable weight, from `@fontsource-variable/nunito` (SIL Open Font License 1.1)                                                                                                                                 |

The game's artwork is part of Globulation 2 and licensed with it under the GPL 3
(see `docs/assets/source-attribution.md` and `data/authors.txt`). Engine-rendered
map previews used by the browser smoke test (`e2e/fixtures/maps/`) were made with
`glob2 map generate <generator> --preview <file> --preview-size 384 --teams N --seed 7`.

## Map Studio imagery

The studio inspiration gallery uses the following engine-rendered repository maps,
compressed with Pillow to WebP (quality 88, at most 640 px on each axis). These are
labeled as terrain inspiration, not promised AI results.

| Output                | Source                                                       |
| --------------------- | ------------------------------------------------------------ |
| `studio-islands.webp` | `docs/map-generators/images/lava-shield/default-256.png`     |
| `studio-hills.webp`   | `docs/map-generators/drumlin-field/256-4-colonies-seed1.png` |
| `studio-river.webp`   | `docs/map-generators/images/hilbert-river.png`               |

`studio-demo-reference.png`, `studio-demo-layout.png`, `studio-demo-crop.png`, and
`studio-demo-ready.png` are recorded artifacts from the deterministic native map
studio integration fixture in `apps/ai-map-worker/test/pipeline.test.ts`. Its provider
returns a native reference without making a paid AI call. The workflow demo labels that provenance; they contain no private user
material. Regenerate with the native integration test with `STUDIO_EVIDENCE_DIR` and copy the initial reference, generated image, crop overlay, and delivered
preview outputs. Preserve these labels when changing the marketing presentation.

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

## Colony video

`colony-loop.mp4` is a silent 1600 × 900, 25 fps recording of the bundled
`data/menu/colony.bin` simulation, drawn by `MenuColony` without menus or status
bars. It replaces the hero's separate walking/flying sprite animations. The
60-second loop dissolves its final two seconds into the opening footage to avoid
an abrupt reset. The two hero WebP posters are the video's opening frame.

Rebuild with the native harness and FFmpeg on PATH:

```sh
scons --build=build/native-tests release=1 server=0 menu-colony-harness
python3 tools/record_menu_colony.py --harness build/native-tests/test/MenuColonyHarness
```

Three lossless review frames stay in ignored `artifacts/menu-colony/frames`;
pass `--keep-frames` to retain all capture frames. Commit the encoded
video and posters. The public website's `public/brand/` receives identical copies
of these three assets; its Astro homepage also plays the recording. Run the video
capture after `build_art.py`, which otherwise restores the original still posters.

## Icons

Navigation and other interface glyphs are not artwork: they are the game's Tabler
outline icons, imported as SVG from `datasrc/icons/tabler/` by `src/icons.tsx` and
drawn in the text colour, so they follow the theme. To add one, add it to that
directory's `manifest.json` and rerun the exporter (see
[UI framework](../../../../docs/development/ui-framework.md)), then import it in
`src/icons.tsx`; `test/icons.test.tsx` checks the files against the manifest.

Use game artwork for decoration: page headers, stat tiles and empty states. Pick a
sprite that fits the page, and avoid giving two pages the same one.

AI studios share the shell and interaction components in `src/components/studio/`.
Use labelled Tabler actions consistently, keep text on primary and destructive
actions, and give compact icon buttons accessible names and keyboard tooltips.
Studio surfaces, focus indicators and status colours use the selected light/dark
theme; statuses also need text. Preserve reduced-motion and forced-colours rules.
Test independent scrolling, keyboard tabs, separator presets, dialog focus return,
IME composition, narrow reflow and zoom when changing workspace controls.
