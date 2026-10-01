# JavaScript second-pass review evidence

Source: `a16a330d6eb38d4c96fff7bd67f5c315b2f6d8c7`, PR [#478](https://github.com/Globulation2/glob2/pull/478). This branch holds generated evidence separately from the source branch. It does not establish completion of the outstanding gates.

The 14 platform archives retain the shared production-runtime/engine corpus: macOS ARM64 and Rosetta x86-64; Ubuntu GCC 13, GCC 15 and Clang 18; Chromium, Firefox and WebKit; both ARM64 and ARMv7 on Acer Android 7/API 24 and Samsung Android 16/API 36; an explicitly selected Android x86-64 emulator; and iOS ARM64 Simulator. Each records source revision, clean status, compiler/build configuration, fixture and executable hashes, commands, logs, test results, numeric bits/data, traces, saves and replays. The JSON comparisons in `validation.json` require successful execution on this same revision and show all 44 compared artifacts equal. Cross-platform decoded saves exclude only the 20-byte MapHeader SHA1.

Extract a platform archive and rerun the comparison from this source checkout, for example:

```sh
python3 test/check_javascript_evidence.py macos-arm64-a16a330d6 browser-chromium-a16a330d6 --output comparison.json
```

`released-and-cli.tar.gz` retains released non-JavaScript traces on macOS ARM64/x86-64 and Linux GCC 13/15/Clang, both 256-tick scripting fixtures with exact one/four-worker replay/save equality and all six CLI continuation checkpoints, and three browser 1500-tick traces equal to native bytes. Fresh released traces compare complete checksums; the v108 continuation compares every stored team/entity record, excluding its version-dependent aggregate. Historical save fixtures exercised are v88/v108/v121; the minimum version 58 constant is checked, but a historical v58 fixture was unavailable. The accepted released v123 replay is executed, and replay/network boundaries are directly tested.

`builds-and-diagnostics.tar.gz` contains build configuration and full/incremental build logs, regression failure logs, vendor reconstruction results, the ordinary test-runner results, and mobile diagnostics. Production runtime and fixtures were unchanged after revision `0c26fc6dd`; subsequent commits corrected CI packaging, a test metadata banner interfering with XML, and browser artifact-export timing. Final executions use the stated clean revision, and affected harness objects were rebuilt. `source.bundle` contains the second pass relative to original PR head `147a99f66dcd2f2d814c1f4e409686f49551f6d6`; fetch that prerequisite from the source PR branch before fetching this bundle. `SHA256SUMS` verifies the archives and manifests.

The pass remains incomplete. Windows execution in the existing Vagrant VM awaits approval to restart VirtualBox after its VERR_SVM_IN_USE failure; a restart could discard unsaved VM state. Physical iOS execution needs an Xcode developer account/profile for `org.globulation2.glob2.script-tests` and a paired, unlocked device in normal mode. Simulator results do not substitute for that run. [Final-revision CI](https://github.com/Globulation2/glob2/actions/runs/36823888207) and fresh independent maintainer review are also outstanding. No merge or store publication was performed.
