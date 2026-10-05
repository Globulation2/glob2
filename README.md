# CPU AI Music Studio — implementation evidence

Draft implementation PR: https://github.com/Globulation2/glob2/pull/763

- Tested head: `768004d78ad4db54f378751cb4702c20f510e680`. Runtime sources are unchanged since `c669c74f8`; subsequent commits correct an optional-section test assertion and expand CI/checkout-return coverage.
- Integrated base: `3607e90980ad42766b6d5663ac41e1a2b93e0f93` (shared WebP assets, deployment and protocol updates merged; migration collision resolved as `0042_music_studio`). Subsequent master translation changes are unrelated and were not merged merely to advance the base.
- Host: Ubuntu 26.04.1 LTS, Linux 7.0.0-31-generic, x86_64; Node 22.22.1 for Vitest/TypeScript, Node 24.19.0 with native TS for E2E and render harnesses; Python 3.12.14; PostgreSQL 16; Docker 29.1.3.
- Dependencies: platform package-lock, music requirements.txt and requirements-synth.txt, pinned samples/Surge/sfizz inputs. Optional worker image is Ubuntu 24.04 / Python 3.12 / Node 22.23.3, Linux amd64. No GPU or runtime dependency downloads.
- Final image: `sha256:53a7dc0ff22ffa069adf55e64b6102019110a9f42be917fff7d7cbcb4f910291`. Its runtime source matches the tested head; later commits only change tests. Build used the named build cache for pinned assets; cache contents were SHA-verified by the installer.

## Verification

Commands run from the repository root, except where `cd` is explicit. Logs are in [logs](logs).

```sh
npm run test --prefix platform -- packages/music-studio/test packages/billing/test/products.test.ts packages/db/test/schema.test.ts packages/protocol/test/fixtures.test.ts apps/ai-music-worker/test/pipeline.test.ts apps/api/test/musicStudio.test.ts apps/api/test/music.test.ts apps/api/test/accountExport.test.ts apps/api/test/studioEvents.test.ts apps/worker/test/reliability.test.ts packages/engine/test apps/web/test/musicStudio.test.tsx apps/web/test/musicStudioStream.test.tsx apps/web/test/musicPlayer.test.tsx apps/web/test/musicPlayback.test.ts
# 83 passed, 14 files, exit 0. Vitest ignores unmatched path filters.
npm run test --prefix platform -- packages/music packages/map-studio apps/worker/test/reliability.test.ts apps/api/test/accountExport.test.ts
# 37 passed, 5 files, exit 0; overlaps the focused run.
npm run typecheck --prefix platform
npm run lint --prefix platform
# Both exit 0.
PYTHONPATH=tools/music tools/music/.venv/bin/python -m unittest discover -s tools/music/tests
# 134 tests, OK, 4 optional skips, exit 0.
python3 -m unittest discover -s test/build_system -p 'test_ci*.py'
# 87 tests, exit 0. Includes new authoring worker/pipeline selector cases.
python3 -m unittest discover -s test/deployment -p 'test_*.py'
# 34 tests, 1 failure: unchanged HostDriverTests.test_deploys_detached_and_reports.
# Its unchanged driver reports ('lost','','') instead of ('done','0','none').
# Repeated with test_online_deploy.py: same failure (13 tests).
```

Browser commands used Node 24.19.0 at the front of PATH:

```sh
npm exec --prefix platform --workspace @glob2/web -- vite build
SCREENSHOT_DIR="$PWD/artifacts/music-studio-visual" npm run --prefix platform --workspace @glob2/web e2e -- music-studio.spec.ts
# Production build exit 0; desktop and phone both passed, exit 0.
```

The E2E uses a real built application/seeded login with mocked studio endpoints (no provider/payment calls). It covers history inspection, single-player comparison, draft/parent persistence, explicit publication, detailed measurements, overflow and Axe accessibility. Checkout return restores the draft, parent and pending submission UUID; the return URL grants no credits and causes no automatic resubmission. The reconnect banner in the [screenshots](visuals) is expected because the mocked event stream closes. Hook tests separately exercise ordered SSE replay, duplicate events, reconnect snapshots, terminal recovery, authorization/stream lifecycle and celebration behavior. Existing player tests exercise actual decoding/playback behavior independently of the visual fixture.

Recipe isolation was tested as UID 10001 inside a parent user/mount namespace, with a dedicated 6 GiB tmpfs:

```sh
unshare --user --map-root-user --mount sh -c 'mount -t tmpfs -o size=6g tmpfs /tmp/glob2-music-test-mount && unshare --user --map-user=10001 --map-group=10001 sh -c "MUSIC_SCRATCH=/tmp/glob2-music-test-mount MUSIC_SANDBOX_TEST=1 npm run test --prefix platform -- apps/ai-music-worker/test/runner.test.ts"'
# 1 passed, 1 opt-in render test skipped, exit 0.
```

The probe rejects network/credential access, writable dependencies/root, process excess, runaway execution, excessive memory/output, symlink escapes, malformed score data and forged recipe reports. No host mounts or security policy were changed outside the disposable namespaces.

```sh
docker build --target ai-music-worker -f deploy/Dockerfile -t glob2-ai-music-worker:local-validation .
docker run --rm --network none --read-only --tmpfs /tmp:size=128m,mode=1777 -e HOME=/tmp -e OPENBLAS_NUM_THREADS=1 -e OMP_NUM_THREADS=1 glob2-ai-music-worker:local-validation /opt/music/bin/python -m glob2music.studio probe --job /tmp --cache /opt/music-assets
# Both exit 0; packaged sfizz and Surge load without network access.
```

A separate container `createRunner` startup probe under default Docker security policy fails with Bubblewrap namespace denial. The test asserts that denial and that no jobs are accepted. **This is not evidence of a working production container isolation policy.** Operators must supply a reviewed policy/host that permits the required namespaces; unrestricted execution is not a fallback.

## Real CPU audio evidence (not service qualification)

These runs use approved local recipes and manually targeted repairs, not a live LLM. No provider tokens or provider spend were incurred. The sandbox, data boundary, real renderers, mastering and independent encoded-byte QA were exercised. Reports contain all score findings and ten audio checks, exact encoded-track SHA256 values and frame counts. Packaging audit verifies that each successful ZIP contains exactly the three encoded tracks recorded by QA.

| Candidate | Result | Elapsed seconds | CPU seconds | Peak single-process RSS GiB |
| --- | --- | ---: | ---: | ---: |
| acoustic-final | pass with warnings | 588.4 | 327.8 | 7.60 |
| synth-v1 | blocked: seam failure | 238.1 | 238.7 | 3.60 |
| synth-revision | blocked: seam failure | 292.8 | 291.6 | 3.60 |
| synth-revision-2 | pass with warnings | 735.7 | 467.9 | 3.61 |

Elapsed times were measured on a shared loaded host, not the two-CPU deployment container. Peak RSS is the maximum process resource counter, not total cgroup peak memory. Do not derive credit prices from these numbers.

- [Acoustic report](renders/acoustic-final/report.json), [listening preview](renders/acoustic-final/preview.opus), [source](renders/acoustic-final/composition.py): current trusted rendering code; warnings remain visible.
- [Synth baseline](renders/synth-v1/report.json): combat seam flux rank 100 fails the unchanged threshold.
- [First repair](renders/synth-revision/report.json): percussion -9 dB / arpeggio -3 dB still fails (rank about 99.960).
- [Second repair](renders/synth-revision-2/report.json), [listening preview](renders/synth-revision-2/preview.opus), [source](renders/synth-revision-2/composition.py): percussion -12 dB / arpeggio -6 dB, first/last-bar intensity 0.25; seam rank about 99.880 passes with a warning. Seed and melodic notes are preserved.

Exploratory synth runs started before the final trusted title-from-score metadata change. Their audio/QA algorithms match, but these artifacts are retained as repair evidence, not claimed as final-head end-to-end release qualification. Render harnesses are in [commands](commands); run them from repository root in the namespace setup above with Node 24. Their explicit metadata are test fixtures (the acoustic harness inherited generic synth fixture text); actual service metadata uses the appropriate pipeline/sample credits. Full test downloads are deliberately not offered here as qualified releases. Preview files are solely listening evidence.

## Coverage and remaining work

Focused coverage targets independent balances, checkout/webhook replay and refunds/disputes, concurrent idempotency, atomic reservation/settlement, cancellation, lease recovery and uncertain provider journals, private access and explicit publication, deletion/retention, generated-release immutability, bounded repairs, synchronization and exact-byte packaging. Existing community conversion/local recipe and Map Studio suites were included where interfaces overlap.

Real LLM generation/targeted revision qualification, actual model cost, Stripe test-mode end-to-end transactions, production namespace policy validation, and **human auditioning remain pending**. Both `enabled` and `salesEnabled` default off; no prices are selected without measured provider qualification. No maintainer listening acceptance is claimed. No Windows/macOS/GPU worker coverage: the optional worker is explicitly Linux amd64 CPU-only. No native simulation/save/replay verification is claimed or required by these authoring-only code changes; no simulation revision bump.

[SHA256SUMS](SHA256SUMS) records attached evidence bytes. This branch contains review artifacts only and must not be merged into master.
