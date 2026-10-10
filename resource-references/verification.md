# Resource reference verification

Source 47794954f2ad12c1477a19ef07eb7871cc50bd59; fetched base 49647ba1d8c034981cbf9e8ab9ec6e86011d4ba6. macOS arm64, Node 24.14.0, existing pinned platform dependencies, isolated PostgreSQL 16 at 127.0.0.1:55432.

From platform: `npx vitest run apps/ai-building-worker/test/pipeline.test.ts --reporter=json --outputFile=../artifacts/building-studio/depot-e2e/resource-references-tests.json` (11 pass, two opt-in tests skipped); `npx tsc -p tsconfig.json` passes. `npx eslint apps/ai-building-worker/src/pipeline.ts apps/ai-building-worker/test/pipeline.test.ts` and Prettier check both pass.

`artifacts/building-studio/depot-e2e/docs-check-venv/bin/python tools/check_docs.py`: 307 documents, zero errors, 74 external links enumerated. `git diff --check` passes. No simulation source or dependencies changed; native/platform matrices omitted. Prompt coverage verifies the actual engine reference text reaches the provider, and restart recovery uses the expanded prompt. Live model behavior will be checked after deployment; deterministic tests do not prove provider accuracy.
