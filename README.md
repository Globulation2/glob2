# Stack smoke skin render worker verification

Tested head: 51b29580914945126d54491ec6403857e3c476d4. Base: 5b4bf8957d7dd917aed781a7bccfcfed03265335; fetched again after testing, unchanged.

Ubuntu 26.04.1 x86_64; Python 3.14.4; Docker 29.1.3; Compose 2.40.3. Repository Dockerfiles and locked dependencies; native engine GCC 13 in Ubuntu 24.04 image. New isolated image tag, eight C++ build jobs, unique Compose project and ports; fresh image creation from the tested checkout, compatible Docker build cache used.

Commands (all exit 0):

```
python3 test/deployment/platform_stack_smoke.py --tag ci-smoke-skin-worker-51b295809 --jobs 8 --log-dir artifacts/stack-worker/final --match-e2e
python3 -m unittest discover -s test/deployment -v
python3 -m unittest discover -s test/build_system -p test_ci_policy.py -v
```

Nine stack checks passed; image build 205 seconds, startup 27 seconds. New skin render worker is healthy. Includes database migrations, edge routing and private endpoint denial, guest authentication/realtime/JWKS, two relays, native map generation, rated match, verified match record and rating updates. Deployment tests: 34 passed. CI policy tests: 19 passed. Project removed with its own volumes after verification.

Change only adjusts the smoke fixture's explicit build service list and image tag. It does not change simulation, save format or protocol; SIM_REVISION and golden match record remain unchanged. No Windows/macOS/Android or full browser rerun for this fixture-only repair, no skin rendering feature acceptance claimed. Current hosted master has separate previously repaired music/Android failures and an unexplained Chromium page crash; this evidence does not establish whole-master recovery.

Original failure retained: https://github.com/Globulation2/glob2/actions/runs/37271070460/job/111640753959

Maintainer acceptance: Codex accepts this local evidence as sufficient for the fixture change under AGENTS.md.
