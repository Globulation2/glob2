# Colony playback repair

Initial game PR745 and website PR9 are merged. Follow-up game PR748 and website
PR10 preserve resume controls when pausing cancels a pending play promise.

A deterministic regression reproduced the AbortError race before repair in all
three engines (pause-race-red.log). Ignore only expected DOMException AbortError;
other rejection and media error paths retain the poster fallback. The fallback
test now waits for an actual media error.

Linux WebKit exposed a separate test-server transport failure: it reopens the
video using Range bytes=18834-, but the Firebase-header server returned only
whole files. Website repair implements single byte ranges,206/416 responses,
HEAD behavior and stream cleanup. Independent review verified exact response
bytes for this open-ended request, closed and suffix ranges, end clamping,
invalid ranges and HEAD. No blocking review findings. Production layout and
video assets are unchanged; original two rendered review rounds still apply.

Website tested head422e1ca8c719a52097fe757b3d7d211a944f43eb, base
8afdb49ffa520804b49857a4b33ba5b15d4b8f7c. macOS26.6.2 arm64,Node24.14.0,
pinned npm dependencies:

```sh
npm run check
npm run build
node scripts/check-links.mjs
node --test scripts/test_monitor.mjs
python3 -m unittest discover -s scripts -p 'test_*.py' -v
npm test
```

All passed. Astro42files,zero errors/warnings/hints. Full120browser checks across
Chromium,Firefox,WebKit passed. Logs in logs/range-site-*.log.

App tested head6b5bf4793693b84b81e04473a849365526727023, base
723be2c4f2056c9c0932d0fdc1abd4ba7ec3bd2c. Rebased onto current master to
include the community music dependencies and test-server changes. Final checks
will be appended below once complete. Changes are TS/React only; simulation,
save/load,replay and network compatibility boundaries are unchanged.

Original hosted failures and traces are retained in linux-webkit-failure/.
Initial broader app phone reflow failure is documented in APP-INTEGRATION.md;
it reproduced on the unmodified original base and is outside these changes.

App final checks passed on the refreshed base:

```sh
npm ci
npm run typecheck
npm run build -w @glob2/web
npm run lint
npm run e2e -w @glob2/web -- --grep 'home shows|home: navigation|colony moves|pending play'
```

All exit0. Eight desktop/phone browser checks passed (13.4s). Logs are
logs/pause-repair-current-*.log. Hosted game cheap contracts are separate from
local behavioral evidence; an expensive hosted engine matrix is not requested
for this TS-only repair.
