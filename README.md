# Browser test type-check repair

Tested commit 800a37707ac11615ea39fab15a93c3ef33f0c5dd, base 1b88ee4009a7fa373da5dd9bf18562ef4ea58ac4; master fetched unchanged before final verification.
Ubuntu 26.04.1 x86_64, Node22.22.1, TypeScript6.0.3. Dependencies installed from committed lockfile with npm ci --ignore-scripts.

Hosted baseline: https://github.com/Globulation2/glob2/actions/runs/37408019668/job/112092462629 (six DOM API errors in music-decoder.spec.ts). Local initial baseline additionally found missing workspace links from stale dependencies; npm ci corrected local dependencies. No unrelated source repairs or lockfile changes.

Exact commands from platform/:
- npm ci --ignore-scripts: PASS
- npm run typecheck: PASS (server, web, browser end-to-end projects all checked)
- npx eslint apps/web/e2e/music-decoder.spec.ts: PASS
- npx prettier --check package.json tsconfig.json apps/web/e2e/tsconfig.json: PASS
- node node_modules/typescript/bin/tsc -p apps/web/e2e/tsconfig.json --showConfig: retained; includes music-decoder.spec.ts and all end-to-end TS files, strict settings preserved.
- node node_modules/typescript/bin/tsc -p tsconfig.json --showConfig: retained; server ES2023-only libraries preserved, browser cases moved to explicitly checked project.
- git diff --check: PASS.

No app/test runtime or compiled simulation code changed, so browser execution, database integration and native matrix were omitted locally. Full type check is the failure's direct oracle. Logs and effective configs attached in evidence.tar.gz. No simulation version change required.
