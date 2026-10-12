# Scripting verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## JavaScript

See the [scripting guide](../../scripting/javascript.md) and
[API reference](../../scripting/javascript-api.md) for the public boundary.

Format 127 introduced counted generation tables for sixteen teams.
Version-gated loading preserves released format 124's experiment-header layout
and remaps the twelve-team generation tables stored by formats 125 and 126.
Formats 58–124 receive scripting identities on load; later formats validate their
stored identities and generation tables. The current save, network and replay gates live in [Version.h](../../../src/app/Version.h) and [ReplayReader.h](../../../src/replay/ReplayReader.h). Draft JavaScript
fixtures use format 125; released historical fixtures remain unchanged.

Build `unit-tests engine-tests` with SCons and run
`python3 test/run_tests.py --build-dir build/darwin/client/release --filter 'JavaScript*/*'`
(use the build directory for your platform).
The runtime harness checks capability restrictions, deterministic work exhaustion,
automatic global snapshots, aliases/cycles, reload/rejection rollback, serial
worker migration and exact Math output bits.
Raw global-number fixtures also compare the persisted snapshot boundary:
NaNs have one canonical representation, while signed zero and infinity signs
survive save/load. The integration harness checks AI
visibility/ownership and transactional scenario effects and continuation. Run
`python3 test/check_javascript.py /absolute/path/to/glob2 --output artifacts/js-check`
with a fresh output directory for the frozen per-tick profile trace, worker
equivalence and full-game saved continuation.


`python3 test/run_tests.py --filter 'ScriptEditor/*'` checks the map editor's
SGSL/USL/JavaScript language selection, draft compilation and cancellation,
`.js` load/save, embedded map source/mode round trips, and dropdown interaction
with captures on desktop and both phone orientations. It also exercises a real
USL runtime resource failure during JavaScript-to-SGSL confirmation, verifies
that both live programs survive the failure, and executes the committed SGSL
program after its preparation objects are destroyed. SGSL exchange coverage
checks story owner pointers in both resulting runtimes.

The named `JavaScriptNumbers`, `JavaScriptTransactions`, `JavaScriptLifecycle`,
`JavaScriptRealistic`, `JavaScriptPresentation`, `JavaScriptSession` and
`JavaScriptSimulation` suites run alongside runtime/integration cases. The shared
corpus includes real map-reading economic planners and a scenario survey with
transcendental math, private RNG, returned data and executed orders. Browser and
iOS harnesses link the same production objects and select these suites; Android
uses the same native test registry.

`python3 test/check_javascript_corpus.py --build-dir BUILD --output artifacts/js-corpus`
retains numeric bits, serialized results, simulation traces, saves, replays, logs,
JUnit results, source/fixture hashes and compiler metadata. Harnesses emit their
build-time source revision and content hash; runners reject binaries built from
a different source tree. The shared comparator also requires identical normalized
Git source hashes across platforms. Git blob normalization accounts for checkout
line endings and symlink representations; modified and untracked inputs still
make development builds ineligible for the clean revision gate. Manifests retain
the actual Git status entries, and CI records checkout status before and after
compilation, so unexpected dirty inputs can be diagnosed without relaxing that
gate. A clean runner checkout alone does not establish binary provenance.
The native corpus runner requires a clean
committed revision; `--allow-dirty` is for development evidence only. Use fresh
output directories and compare identical final revisions across platforms.

Android: build `android-tests` for API 24 and the selected ABI, then run
`python3 mobile/android_device_tests.py --android-sdk SDK --serial SERIAL --arch ABI --suite 'JavaScript*' --output artifacts/js-android`.
The runner uses disposable shell directories, retrieves artifacts even after
failure and never accesses installed game data. Use a fresh Android output
directory; failed artifact transfers fail the run and retain its remote evidence
for recovery. iOS: build the separate app with
`python3 mobile/ios.py build --environment simulator --release --script-tests`;
for a device use `--environment device --team TEAM`. Its bundle identifier is
`org.globulation2.glob2.script-tests`, and evidence is exported in its own
Documents/ScriptingEvidence directory. Simulator evidence does not satisfy the
physical-device gate. Browser: build `web-tests` and run the shared corpus case in
`browser/tests/determinism.spec.js`; evidence is under
`artifacts/browser-determinism/script-corpus/`.

Cross-platform acceptance requires identical numeric/data results and complete
per-tick traces, plus decoded save payloads. Exclude only documented MapHeader
SHA1 metadata when save histories differ. Same-platform equal-history save and
replay bytes must match. Build success and simulator-only runs are insufficient.
CI retains evidence even when execution fails; unavailable devices/signing leave
those platform gates incomplete. See the [fixture notes](../../../test/fixtures/javascript/README.md)
for the exact frozen worlds, seeds and intended draft profile corrections.
CI executes the shared scripting corpus in Chromium, Firefox and WebKit and
compares their numeric/data results, complete traces and decoded saves against
the Linux and Windows corpus runs. The separate released replay comparison
selects only its baseline traces, so scripting fixture traces cannot be mistaken
for the released replay.
The same evidence comparison requires the frozen 150-row custom-resource
composition trace from every selected native platform and both serial and
threaded runtimes in Chromium, Firefox and WebKit. Native collection uses
`test/run-browser-determinism.py BINARY OUTPUT --engine-binary ENGINE_TEST_BINARY`;
its fresh `resources/native` directory retains the command, embedded build
provenance, JUnit result and complete trace. The comparison rejects missing or
repeated browser identities, dirty or mismatched source producers, failed runs,
and truncated or changed traces against the preserved committed fixture.

`python3 test/check_javascript_evidence.py REFERENCE CANDIDATE --output artifacts/js-comparison.json`
compares shared numeric/data values, complete traces and decoded save payloads,
requiring the same artifact inventory. It excludes only MapHeader SHA1 from save
payloads. Review each runner manifest to establish matching source revisions and
successful execution before treating matching hashes as acceptance evidence.
Use `mobile/ios_script_tests.py` with an explicit device identifier and, for a
simulator, an owned `--simulator-set` to install, run and retrieve the separate app.

To retain released simulation compatibility traces, replays, commands, and logs,
pass `--output artifacts/released-compatibility` to
`test/check_telemetry_simulation.py`. Fresh-load traces compare complete bytes;
the legacy checkpoint comparison also checks complete bytes, including the
aggregate checksum and every stored team/entity record. The current references
record simulation revision 34's delayed resource growth, scheduled building gradients
and greedy fetching;
historical version-123 references remain separate. CI retains these artifacts even when verification fails.

The shared evidence comparator requires successful runs of the same clean source
revision. `--allow-development` permits diagnostic comparisons while recording
provenance failures; those comparisons do not satisfy the final acceptance gate.
