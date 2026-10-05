# Final app integration verification

Tested commit: `9647651430a5429d11b2052ab09fe52425bec88f`.
Base: `b58be00a245f801442ab330bf0e4b9e14dfa431b`, rebased PR head.
The refreshed base includes new Opus audio dependencies and skin-editor changes.
Environment: macOS 26.6.2 arm64; Apple clang 21.0.0; Node24.14.0;
SDL3 prefix, pinned recording libraries and npm lockfile; Homebrew opusfile0.12_1
for the updated base. Native `release=1 server=0`, software SDL dummy renderer.

Commands and outcomes:

```sh
cd platform
npm ci
npm run typecheck
npm run build -w @glob2/web
npx eslint apps/web/src/components/Colony.tsx apps/web/src/art.tsx apps/web/e2e/smoke.spec.ts apps/web/e2e/server.ts
SCREENSHOT_DIR=<artifacts/menu-colony/integration-app-review> npm run e2e -w @glob2/web -- --grep 'home shows|home: navigation|the colony moves|phones:'
cd ..
GLOB2_SDL3_PREFIX=<SDL3-prefix> scons --build=build/native-tests -j10 release=1 server=0 menu-colony-harness
SDL_VIDEODRIVER=dummy build/native-tests/test/MenuColonyHarness check data/menu/colony.bin
```

Install/typecheck/build/lint/native build/native checks exit0. Six desktop/phone
home, navigation/accessibility and real-video pause/reduced-motion tests passed;
one desktop-only skip. Reviewed updated home screenshots in both themes.

The broad phone reflow test exited1 at `/skins` at320px because an untouched
skin-editor input is240×27px (below44px height). This exact failure was independently
reproduced on unmodified master `b58be00a2`:

```sh
git switch --detach origin/master
cd platform
npm run build -w @glob2/web
npm run e2e -w @glob2/web -- --grep 'phones:' --project phone
```

Master build exit0; phone test exit1 with the same input. PR restored afterwards.
`logs/base-phone-regression.log`, `base-phone-failure/` and
`app-integration-failure/` preserve both failures and browser traces. This is an
existing base regression, not introduced by the colony-video patch. No unrelated
skin-editor code was changed. The repository permits merging validated changes
while existing master regressions are documented and repaired separately.

Initial base `87a8b2528` passed the broader reflow test before the new skin editor
landed. Current head passes all focused changed-feature checks. Website codec and
motion behavior passed114 browser tests acrossChromium/Firefox/WebKit at its
unchanged base; its two rendered review rounds passed. Shared assets unchanged.

Coverage limits: native macOS/software renderer only; no native Windows/Linux/GPU
matrix or expensive hosted CI requested. The optional draw argument preserves
default rendering flags and changes no simulation state, rules, saves, replay or
network boundaries. No SIM_REVISION bump required. Cheap hosted contracts pass,
which are not claimed as engine verification.

Author accepts focused evidence for this final revision under AGENTS.md, with the
existing master skin-editor failure explicitly retained. User authorized merging.
