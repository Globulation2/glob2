# Landscape picker performance verification

Branch: `codex/landscape-selection`, based on master commit
`c2c086ad5b08bace7c27399d4eaa0ffd3f950d45`.
Host: Apple M3, macOS 26.6.2, Apple clang 21.0.0; optimized native build.

## Changes

- The native picker uses one worker and releases its queue after the first layout.
- Pending work follows distance from the current viewport center. Sorting,
  filtering, resizing and scrolling change priority without changing seeds.
  Scheduling uses actual card bounds, including single-column layouts.
- Picker thumbnails do not animate. Shared `MapPreview` defers rendering-surface
  allocation until a visible paint, and the desktop grid skips off-screen drawing
  and text layout while preserving navigation/hit metadata.
- Cooperative/browser polling advances one attempt, including a single retry,
  at a time. The picker only polls cards in view, leaves 100 ms between attempts,
  and postpones work during interaction. Native retries also return to the queue,
  so obsolete requests and changed priorities take effect between attempts.

## Native benchmark

The harness injects 67 completed 256×256 thumbnails with fixed pixels into the
real picker at 640×480, then scrolls 80 frames through the grid and back.
This isolates delivery/rendering costs; it is not a map-generation or overall
frame-rate benchmark. Other processes/builds were running, so timings vary.

| Measurement | Baseline | Changed |
| --- | ---: | ---: |
| Initial completed-thumbnail delivery | 95.504 ms | 13.182 ms |
| Rendering surfaces allocated initially | 67 | 6 |
| Median scrolling frame | 12.889 ms | 1.420 ms |
| 95th-percentile scrolling frame | 15.397 ms | 9.191 ms |

See [baseline log](base.log) and [changed log](fixed.log).
A second run that retained screenshots measured delivery at 128.295 → 13.442 ms
and median scrolling at 16.699 → 1.519 ms. See
[baseline capture log](base-capture.log), [changed capture log](fixed-capture.log),
[baseline scrolling image](base-capture/middle.png),
[changed first view](fixed-capture/top.png), and
[changed scrolling image](fixed-capture/middle.png).
The final regression run, including assertions that all three screenshots exist,
is [verified.log](verified.log).

The new off-screen allocation assertion fails on the baseline (67/67 surfaces)
and passes on the change: [baseline failure](base-regression.log).

## Correctness checks

- [Queue checks](queue.log): deferred startup, center priority, scrolling/filtering,
  grid and single-column bounds, viewport-only cooperative polling, stable seeds,
  unchanged successful output, yielding retries, reroll/restart, and removing an
  in-flight slot before joining its worker.
- [Map comparison](map-comparison.txt): Isles, River and Contested Commons with
  root seed 71, 256×256 and four colonies produce identical successful seeds,
  terrain hashes, scores and colony positions before/after.
- [Headless shared-preview tests](map-headless.log): thumbnail codecs, malformed
  inputs, online states, cache behavior and geometry.
- [Shared-preview visual/event tests](map-visual-final.log): first-image and reroll
  fades outside the picker, marker/terrain transitions, gestures, failure states,
  snapshot previews and local-selection caching.
- [Native build](build-fixed.log), [final harness rebuild](build-test-final.log),
  [WebAssembly build](build-web-base.log), and [final WebAssembly check](build-web-final.log).
- Chromium landscape-picker → lobby → start-quality → game navigation passes with
  [WebGL2](browser-webgl2.log) and [software rendering](browser-software.log).
  Both browser test output directories retain screenshots.
- Changed production C++ files pass clang-format checks; `git diff --check` passes.

## Reproduction

Build with `CCACHE=1 scons -j4 release=1 server=0 custom-setup-test map-preview-test`.
Use `run-native.py` to create disposable homes/work directories and retain output
under this artifact directory, for example:

```sh
python3 artifacts/landscape-performance/run-native.py build/darwin/client/release/src/CustomGameSetupHarness queue preview-queue
python3 artifacts/landscape-performance/run-native.py build/darwin/client/release/src/CustomGameSetupHarness verified landscape-responsive
python3 artifacts/landscape-performance/run-native.py build/darwin/client/release/src/MapPreviewHarness map-visual-final map-visual
```

The baseline binary `CustomGameSetupHarness-base` uses saved pre-change UI objects
and the same unchanged engine/library objects. `baseline-sources/`,
`baseline-compile.json` and `baseline-link.json` retain the
source inputs and exact commands. Compiled objects and binaries remain local and are not included on this evidence branch. `landscape-performance` runs the fixture without the
new behavioral assertions, so baseline and changed builds can both report timings.

The browser was built using the repository's pinned Emscripten SDK, served from
`build/emscripten/client/release`, and tested with the existing Playwright
`landscape picker` test. `GLOB2_TEST_RENDERER` selected `webgl2` or `software`.

## Limits

An individual browser generation attempt remains synchronous and may still pause
input. Native cancellation waits for the current attempt to finish. The mobile
branch was not modified, and mobile hardware, Windows, Linux, Firefox and WebKit
were not tested in this session. No generator algorithms, save formats, replay or
network versions were changed. The measurements do not establish cross-platform
simulation equivalence or performance on other devices.
