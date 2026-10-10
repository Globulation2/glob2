# Maxima CLI 2 test repair

Revision bff3be03ce03d4465ffc68e20340d879990fb9c5; base bc7534b5b28d347a6ec8eb9e2aa23fae30cf9621. Fresh master fetch unchanged. Linux x86_64, Python3.14.4. Uses hosted web-native artifact11659933861 from full run38018550179 at69ef3e1b2bf453aa280a69be72f83167b1511d9d (same production CLI/Maxima/data sources as base; only intervening unrelated smoke/formatting/Macrelease fixes). Binary and runtime assets extracted from web-native.tar.gz, using existing build/sdl3-ci/prefix/lib for SDL dependencies.

Before command: LD_LIBRARY_PATH="$PWD/build/sdl3-ci/prefix/lib" artifacts/maxima-cli2-runs/native/build/linux/client/release/src/glob2 dev random-games 1 --map SmallForTwo --matchup maxima,maxima. Exit2 with Unexpected positional argument '1'.

After command: LD_LIBRARY_PATH="$PWD/build/sdl3-ci/prefix/lib" GLOB2_ASSET_DIR="$PWD/artifacts/maxima-cli2-runs/native/build/linux/client/release/runtime-assets" python3 artifacts/maxima-cli2-runs/run-case.py. This executes the exact affected unittest method against the hosted game binary, including both player strategy assertions. It bypasses class setup for the unrelated strategy-dump executable, which is not present in this artifact. No test-body assertions omitted. Final revision test PASS, one case, no failures/errors/skips.

No engine build necessary for this test-only argument correction. Full policy suite, native matrix and save/replay checks omitted because production behavior is unchanged; no SIM_REVISION change. Hosted GCC11/GCC13 failures preserved in linked logs; subsequent master runs confirm recovery. git diff --check passes.
