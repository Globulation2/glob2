# Final fresh-build evidence

PR #740 head f211c818a4fe886ce011f6463623236e748f4f3d, base 7302aae6d26937ef477bc8e928d026bb2f058c2c. Hosted tested merge cffcd7c17b7ee3409d6f724c64d33bd31adee014. Emscripten 4.0.15 release build, both serial and pthread variants, current master Opus dependencies; complete commands, flags and dependency versions in web-build log. The actual SCons --post-js link includes the shim. No manual modification of this downloaded artifact.

Fresh original artifact: https://github.com/Globulation2/glob2/actions/runs/37243334725/artifacts/11317974768

Hosted Chromium serial: 25 tests passed, including colony meshes/context restoration. Hosted threaded Chromium: both colony skin and skin asset tests passed. Complete Firefox and WebKit browser jobs passed too:
https://github.com/Globulation2/glob2/actions/runs/37243334725/job/111559919678
https://github.com/Globulation2/glob2/actions/runs/37243334725/job/111559920494
https://github.com/Globulation2/glob2/actions/runs/37243334725/job/111559919725
https://github.com/Globulation2/glob2/actions/runs/37243334725/job/111559919682

Additional local focused verification: Ubuntu 26.04.1 x86_64, Node 22.22.1, Playwright 1.63.0, unchanged colony-skins.spec.js, unmodified fresh downloaded runtime, two passed in 1.0m. Hashes, full output and live/restored screenshots attached here.

From browser/:
```sh
GLOB2_TEST_RENDERER=webgl2 npx playwright test tests/colony-skins.spec.js --project firefox --project webkit --output ../artifacts/skin-fresh-verified
```

The initial local invocation used an incorrect relative symlink path and was interrupted; the completed verification above used the verified absolute link to the fresh artifact.

Fetched master before acceptance: 6d064df681c3bbf8ce0ce9ff46892ed839fdee61; advancement is only #729's native LAN harness, with no overlapping browser, build or dependency changes. No simulation logic changed; simulation checksum equivalence is not claimed by these rendering tests. Supplemental full matrix jobs may still be pending. Prior provisional diagnosis remains separately retained at this evidence branch root; its old manually patched runtime is not used as final integration evidence.
