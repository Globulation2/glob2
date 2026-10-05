# Browser fixtures after terrain revision

PR780 head 2e0005505 (see PR for full SHA), base a88ff621c3959ce74bfe6761bf562d668c5d0b38. Source production simulation from master79c8 (terrain revision14, save/replay format134); latest fetched c2ea87b78 only adds a native test fixture yield. Dependent replay importer repair is PR781, validated together using final production import objects.

Ubuntu 26.04.1 x86_64, GCC15.2 native release/software client built at 6775c8c017a31504800c18c792e48ba4e8196b8e (same simulation as final PR). SDL3.4.16/image3.4.6/ttf3.2.2. Hosted browser artifact web-client11334895058 from full master run37281174773, source79c8d65f52cfea0a91efa3c104f97d79fd5de54d; unchanged browser simulation inputs. Playwright1.63, Node22.22.1. Both native and hosted engines execute the same terrain revision; these PRs do not change simulation computation, version gates or historical compatibility fixtures.

Replay generation, seed42/default unchanged fixture, 1500 ticks:
```
GLOB2_USER_DIR=<isolated profile> GLOB2_REPLAY_PATH=<output>/current.replay GLOB2_CHECKSUM_SIDECAR=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy <current native client> --nox games/cross-replay.game.gz 1500 1
```
Exit0; original initial-save hash recorded in inputs.json. Committed replay is the complete native recording, not patched bytes. Exact footer accepted by repaired native importer; truncated footer/trailing bytes rejected in PR781 integration evidence. The old hosted artifact reproduces a separate preexisting import bug for current telemetry, so browser import/reload do not pass until PR781 is rebuilt. These failures are retained and not claimed as passes. An earlier invocation used /threaded/ as an HTML entry (that directory has no HTML), corrected to the root loader; no product changes from that setup error.

Studio native command:
```
GLOB2_USER_DATA_DIR=<isolated profile> SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy GLOB2_CHECKSUM_SIDECAR=1 <current native client> --run-game --map-file platform/apps/engine-agent/fixtures/ais/two.map.gz --game-seed 19 --player javascript --ai-script 0:examples/javascript/studio-starter.js --player numbi --ticks 1024 --replay true --telemetry checksums --save initial --save final --output-dir <output>/studio-native
```
Exit0. Source/map hashes unchanged. Native trace1849612 bytes SHA256 f01db26d35949e2685ce3a1544848342a4ba9ac5a2106e5f8ab330ba1852f422. Every per-tick checksum byte matches Chromium, Firefox and WebKit, each serial and threaded (all 7 traces attached).

`python3 browser/serve.py 8779 --bind 127.0.0.1 --directory artifacts/replay-fixture/web-client` (COOP/COEP headers); point the ignored build/emscripten/client/release symlink at that same artifact so temporary runtime hosts are served.
`GLOB2_TEST_URL=http://127.0.0.1:8779 npx --prefix browser playwright test --config browser/playwright.config.js browser/tests/studio.spec.js --grep 'Studio source produces deterministic trace' --output artifacts/replay-fixture/final-studio-tests`: 6 PASS,31.1s. An initial local runtime host was written to an older ignored build symlink; corrected before final tests. No trace reference is blessed from browser output: the new digest was computed natively first and then matched independently.

Omissions: full browser suite, rebuilt browser import/reload after PR781, additional native architectures and full master CI. Simulation changed previously in the terrain PR; this repair updates test-only references for that already versioned change. Full hosted master recovery remains to be confirmed.
