# Growth cleanup review before landscape integration

Tested source `8c1f6b7c9ed8719bb8912dffaba5fe5235dc1553`; integrated master `37213ca3cb2dc806a491a77a525323464b96a859`. Linux x86-64 GCC15.2 release client. See freeze.json for build flags, executable and dependency hashes. The build and build-final logs capture the initial rebuild and final committed-revision metadata refresh; verify-growth-merge.py and the command JSONs record exact validation commands. The initial test launch used the older harness executables and was stopped; final suites run after explicitly rebuilding the `tests` target (build-tests.log). Build commands use the default target for the game and `tests` for both harnesses: `CCACHE=1 GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix GLOB2_RECORDING_PREFIX=/home/bradley/glob2-terrain-art2/build/linux/client/release/recording/prefix scons -j8 release=1 server=0 optimized_assets=0`.

## Review and cleanup

Reviewed immutable computation, ordered owner publication, RNG ownership, executor lifecycle, snapshot capture, resource setters/accounting, save-layout probes, replay/network gates and the documented behavior. No blocking architecture issue identified. Existing historical save readers and immediate test/benchmark reference are intentional.

Expanded the dense historical save-layout probe for reviewability, documented its terrain discriminator, added its direct array include, named the integrated format-149 boundary and removed duplicated comments. No simulation behavior change; SIM34, format149, protocol67 and durable save floor58 remain unchanged. Master was integrated because its new Maxima continuation fixture covers this changed simulation boundary.

The Maxima baseline still described SIM33 and failed at tick 30000, while independent midpoint save/reload passed. Refreshed it using archived pre-cleanup executable `61b6ff740` (reference hash in freeze.json), which independently validates the midpoint before writing. The final executable verifies it without fixture updates using compute sizes 1 and 4. See maxima-reference, maxima-zero and maxima-workers for retained traces and saves. The original failing run is retained in maxima-before.

## Results

{
  "engine": {
    "passed": 602,
    "failed": [],
    "skipped": 45
  },
  "golden": {
    "passed": 13,
    "failed": [],
    "skipped": 0
  },
  "unit": {
    "passed": 889,
    "failed": [
      [
        "ImageAssets",
        "16-bit RGBA rounds normalized channels to the exporter reference"
      ]
    ],
    "skipped": 20
  }
}

The ImageAssets native-SDL 16-bit decoder failure, if listed, is the previously reproduced unrelated baseline failure documented in the prior cleanup/final-master evidence. Every other failure must be resolved before merging. Master subsequently advanced with the landscape resource catalog, so this is pre-integration evidence; final integration results supersede it.

Eight fixed 1,024-tick inputs compare exact world and replay traces against archived `61b6ff740` at compute sizes 1 and 4 (continuation.json). This guards against unintended simulation changes in the cleanup. Full native engine/unit compatibility coverage covers the shared format header, load paths, growth pipeline, executor and test doubles; golden cases run without updates. Maxima additionally checks 512 baseline ticks and 256 midpoint-continuation ticks in each execution configuration. Simulation-version gate and whitespace checks pass.

No new Windows/macOS/Android/browser/threadless-build/display qualification, gameplay review or throughput claim. Those limits and the previously measured performance/memory tradeoffs remain explicit. Runtime zero workers on Linux is not a threadless build. User accepted the tradeoffs and requested merging. Existing hosted master runs are left untouched.
