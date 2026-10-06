# PR841 verification

Final tested a1928d6f73d86e8b8da44ff4db6b4af8931ccc97, base/fetched master3bbe89f0e879a2ed42399369caea49c029ca2a3e. Ubuntu26.04.1 x86_64 Node22.22.1, npm ci --ignore-scripts with unchanged platform/package-lock.json. Only platform/vitest.config.ts and platform/apps/web/vite.config.ts change. No shader contents, runtime computation or save/replay/network/SIM changes.

Original hosted failure https://github.com/Globulation2/glob2/actions/runs/37460338614/job/112287075026 : four suites cannot load libgag/shaders/skin-material.glsl?raw, Denied ID outside Vite workspace. Local unchanged checkout reproduces all four suite failures before any tests execute (before.log). After change, same command passes31 tests in4 suites (after.log): npx vitest run apps/web/test/ais.test.tsx apps/web/test/forms.test.tsx apps/web/test/pages.test.tsx apps/web/test/skins-workspace.test.tsx .

Final checks from platform/:
- npm run typecheck (all three TypeScript configurations)
- npx eslint vitest.config.ts apps/web/vite.config.ts
- npx prettier --check vitest.config.ts apps/web/vite.config.ts
- npx vitest run apps/web/test

Development-server probe: createServer using apps/web/vite.config.ts with apps/web root, localhost ephemeral port; fetch /@fs<absolute repo>/libgag/shaders/skin-material.glsl?raw&import returns200 and a JS export default module; /@fs<absolute repo>/AGENTS.md?raw&import returns403. This verifies both intended shader access and retained outside-workspace restrictions. Initial probe used an incorrect /@fs// path and omitted import query; it is retained as diagnostic error, not application failure. Corrected dev-boundary-recheck.log is accepted.

Coverage follows import-boundary risk: affected suites, complete web-unit set, types/config lint, real dev server positive/negative access. No native engine, browser rendering matrix or database-backed full platform suite rerun; original hosted99 other suites passed, and this config change has no simulation or visual computation. No gameplay effect.

Final results: all146 web tests in23 suites PASS; four originally failing suites31tests PASS; all type configurations and config ESLint/Prettier PASS; corrected HTTP boundary probe PASS.
