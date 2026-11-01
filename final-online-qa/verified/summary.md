Final Bulgarian online verification passed for product commit 42c5c0cf63aac32ef584a32566f95063ed24080c, including the compact catalog commit c271876c11. Online catalog SHA-256: 69de8f4cf769924db105fe166cf25a352967e5ca3263951dd1753c3d8f263813. Product source stayed frozen throughout the final build, typechecks and browser tests. provenance.json and bundle-manifest.json bind the inputs and produced files. The preceding final snapshot remains untouched in ../verified-before-compact.

Environment: Linux x64, Node v24.22.1, Playwright 1.63.0, headless Chromium 153.0.8010.12. Desktop 1280×900 and phone 390×844; the layout case explicitly exercises 390×900 and 1280×900. Explicit Bulgarian locale cookies and bg-BG browser locale were used.

Final checks:

- Vite build passed in 31.43 seconds. Focused ESLint, Prettier and set-workspace unit tests passed (7 tests).
- Both fresh exact-source TypeScript projects passed with no diagnostics: platform/apps/web/tsconfig.json and platform/apps/web/e2e/tsconfig.json.
- i18n source/inventory check passed: 37 languages × 2843 messages.
- Browser suite: 13 passed, 1 intentional duplicate-phone layout skip, 3.0 minutes, 1 worker. It includes 20 actual route/viewport layout checks plus actual text-range bounds for technical building labels.
- Compact phone text fits were measured with the loaded UI font: search placeholder 227 px within 260 px, unlisted selection 150 px within 206 px, and experiment-default selection 118 px within 188 px. All desktop measurements also passed; inline-fit-metrics.json records the six results.
- Homepage/sign-in font and language persistence, translated AI filter/card/detail/publish labels, map visibility labels, music Artist and attribution Credits & sources, both terrain/resource Add actions, creation defaults and saved-name persistence passed on desktop and phone. Canonical Rush query values, terrain/resource collections and preset IDs remain unchanged.

Real API coverage includes seeded sessions and histories, server sign-in HTML, player/map/building details, account and upload forms, building draft creation/save/reload, and terrain/resource draft creation/save/reload across English/Bulgarian switches. Only populated AI list/detail responses are mocked with schema-shaped e2e fixture data; their actual UI labels and outgoing filter values are asserted. No provider request, payment, email or external installed-app launch occurred.

Technical identifier boundary: arbitrary schema identifiers and their generic derived labels intentionally remain invariant, for example primaryMaterial→Primary Material and growthRate→Growth Rate. Canonical preset/material IDs such as grass, trees and wood remain invariant too. These are technical fields, not translated interface prose. Fixed Add actions use translated category labels; newly created editable names use Терен: grass / Ресурс: trees. Explicit authored names and previously saved metadata are never rewritten when locale changes.

Eight explicit final PNGs plus screenshots/report.json were inspected directly: default names, compact controls, Add actions, music fields and technical-label wrapping are readable. Browser-owned Choose File/No file chosen captions retain the headless installation's browser UI language and are outside the app catalog. Safari/Firefox, physical devices, provider-backed studios, payment flows and WebAssembly multiplayer rooms are outside this final suite.

Earlier locator and test-type diagnostics are retained separately under ../provisional. They are resolved and excluded from this passing final-source result. The previous compact-predecessor visual evidence was not silently overwritten.

Cleanup completed: API 42769 stopped; only the task-owned glob2-bulgarian-final-online-qa PostgreSQL container/anonymous volume on 42770 was removed. Other jobs, services and databases were not stopped or reconfigured.
