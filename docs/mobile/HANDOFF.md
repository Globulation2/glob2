# Mobile project handoff — 2026-09-08

## Start here in the next chat

Continue in **`/Users/bradley/glob2-mobile`**, branch **`codex/mobile-platform`**.
Read this file, [status](status.md), and [development](development.md) first.
Do not work in `/Users/bradley/glob2`: that checkout contains the user's separate
AI Maxima work. Do not modify the browser checkout either. Keep caches, profiles,
SDK downloads, generated projects and build outputs under this worktree.
Never install over the existing desktop executable.

Repository: https://github.com/Globulation2/glob2
Draft PR: https://github.com/Globulation2/glob2/pull/208
PR base: `codex/browser-experiment`.
Current browser base: `9dc201436` after rebasing the mobile commits.
Browser head `4051adb4d` was inspected during the replay-save pass but not merged.
Check the remote before the next substantial milestone;
inspect its changes before merging. Do not chase a moving base during every build.

The user resumed after the original subscription handoff and asked to finish the
remaining mobile UI implementation. The latest checkpoint below supersedes older
lists of unadapted screens. **This is still a development preview, not a
physical-device-qualified release.** Main/system-menu visual redesign belongs to
another PR. Pinch zoom is explicitly deferred. Use existing game art and shared
simulation/orders.

Suggested first message to the new agent:

> Continue the Glob2 mobile in-game UI work in /Users/bradley/glob2-mobile on
> codex/mobile-platform. Read docs/mobile/HANDOFF.md and the linked documentation.
> Keep AI Maxima and browser checkouts untouched. Finish and qualify the remaining
> in-game UI, maintain compatibility with the browser branch, and update draft
> PR #208 with honest evidence and remaining limitations.

## Latest work: native save synchronization

The user is unavailable for Pixel 6 testing until morning and authorized other
implementation work. Atomic native writes now sync file contents before rename
and the parent directory afterward, with Apple full sync and Windows write-through.
The native harness injects sync failures and kills writers at controlled boundaries.
See the newest [status checkpoint](status.md) for validation and limitations.
The preview will identify this checkpoint in BUILD.json; the previous UI preview
at `8b8f0390d` is preserved under `build/mobile-preview-history`.

Next independent storage work is recovery generations/startup recovery and native
import/export. Do not claim automatic recovery of unsaved game progress, orphan
cleanup, device power-loss safety or storage latency qualification is complete.

## Previous work: remaining authoring UI

This checkpoint follows `e1e152ce0` and adapts the specialized map-editor workspace
and campaign authoring. `PhoneEditor` preserves original edit actions/artwork,
adds a full-width map with explicit pan/edit mode, fixed category tabs, scrollable
tools, large brush/team targets and a larger minimap. The editor's screen host
must explicitly request a 320-point minimum responsive canvas; using the legacy
canvas made the Android editor too small and expensive to render. The harness
now exercises entry through ScreenStack as well as direct editor gestures.

Editor menu/load/save/team/area/scenario dialogs use PhoneForm, including nested
script file dialogs. Editable multiline text is exposed as wrapped UTF-8 rows
with touch cursor placement and keyboard dismissal. Campaign and map-entry
editors now opt into the same presenter. Current verification and artifacts are
recorded at the top of [status](status.md). Native/session and 21 build-system
checks pass; the browser sweep passed 27/27, followed by 12/12 focused persistence
checks after the final Save/Cancel event-batch fix. Android, Wasm and iOS simulator
builds pass. The refreshed `build/mobile-preview` contains the latest signed APK,
matching symbols and exact commit in BUILD.json; its older version is archived
under `build/mobile-preview-history`.

Pinch zoom and the separately scoped main-menu visual redesign remain deferred.
Physical-device, translation/bidi and assistive-technology qualification remain
release gates; a working native harness or emulator is not a substitute. Keep
future work focused on concrete device findings rather than repeatedly calling
already adapted editor screens unimplemented.

## Earlier resumed work: native phone forms

### Resumable replay saves (latest)

The settings/results checkpoint is committed and pushed as `f4f397837`.
The following replay-save checkpoint replaces the results screen's nested event
loop with a `ReplaySaveScreen` child on the shared screen stack. Native phones
use PhoneForm; desktop/browser retain the existing dialog. Failed writes remain
open for retry, errors scroll into view, and browser saves wait for durable
storage. Replay output uses checked atomic replacement and restores the live
buffer position after failure. The format version is unchanged.

Native touch, engine-session and savegame-safety checks pass. Wasm, signed Android
ARM64 and iOS ARM64 simulator release builds pass. The initial browser replay and
viewport run passed 24/24; the final replay-only run passed 9/9 across Chromium,
Firefox and WebKit. See [status](status.md) for evidence and limitations.

Next: adapt the specialized map-editor workspace, then continue device and
remaining results-flow qualification. This pass adds no replay-specific emulator
or physical-device evidence. The current signed APK is in the build path below;
`build/mobile-preview` still contains the older pre-rebase artifact. Pinch zoom
and main-menu visual redesign remain deferred.

### Resumed settings/results work after the browser rebase

The local branch was rebased onto browser commit `9dc201436`; the settings/results
checkpoint is `f4f397837` (phone forms code `2fa19a408`). The older
commit IDs in the sections below describe pre-rebase verification and artifacts.
The equivalent older remote history was replaced with an exact lease when
pushing `f4f397837`; do not merge the pre-rebase history back into this branch.

That checkpoint extends PhoneForm to global settings tabs and end-of-match
statistics, retaining the existing widgets, callbacks and save-preferences path.
It adds slider/key-selector rows, explicit footer selection, colored team rows
and an interactive statistics graph. The resumed test fixture now uses the public
TeamStats API after the browser rebase made its history private. Qualification
for this pass is recorded at the top of [status](status.md). Do not attribute
these changes to the existing Pixel 6 APK: its BUILD.json still identifies the
older pre-rebase phone-form source.

Continue qualifying the remaining results flows and adapt the specialized
map-editor workspace. Physical-device testing remains a separate gate. The new
signed working-tree APK is under
`build/android/device/arm64-v8a/26/client/release/android-project/app/build/outputs/apk/release/app-release-development.apk`.

Commit `4345f28e0` adds `PhoneForm`, `ResponsiveDialog`, shared safe-area geometry,
and persisted 100/125/150% dialog text size. Read the **Phone forms and adjustable
dialog text** section in [status](status.md) and the implementation notes in
[development](development.md). This supersedes older statements below that setup
screens have only their original tiny desktop layouts. The map editor workspace,
global settings tabs and end-of-match statistics still need dedicated adaptation.
Pinch zoom remains explicitly deferred. Do not import the menu-refresh visual
identity or zoom PRs as part of this pass.

## Pixel 6 preview is prepared

The user wants to test soon on a Pixel 6. Code `9a4ad5d4b` reflows tabs and adds
repair/upgrade and loaded-replay control coverage. `b8c0a46f6` fixes the shared
script GC lifetime crash discovered by emulator tutorial reload testing; the
corrected APK passes the same reload sequence. See the newer preview section
in [status](status.md) and [device-testing](device-testing.md). The local handoff
folder is `build/mobile-preview`, containing the signed ARM64 APK, symbols,
BUILD.json and checksums. Prior limitations below describe older checkpoints;
physical-device feedback and broader qualification still remain.

## Resumed Actions qualification

The user resumed implementation after the subscription handoff. Commit
`8315abc86` adds an explicit Info tab for owned-building details and cancels held
actions when building/construction state changes. Its expanded native harness
passes in portrait and landscape: production ratios and limits, clearing toggles,
flag requirements, destroy confirmation/cancel, cancel-destruction and
cancel-construction all emit the original orders. Actual repair/upgrade initiation
and live replay controls still need qualification.

Commit `89bea4560` fixes blank new captions caused by blank lines in the strictly
paired English translation catalog, with a regression assertion. Native touch
and merged engine-session tests pass. The merged Wasm and iOS simulator builds
also pass after the correction. All 30 browser viewport/shutdown/campaign-editor
storage tests pass across Chromium, Firefox and WebKit. The current iOS app was
installed/launched and its main menu inspected in the isolated simulator.
A visual check confirms Actions/Info captions now appear, but narrow tab captions
still wrap awkwardly. Improve that layout without shrinking essential touch targets.

Android's dependency fingerprint rejected the older local manifest. Refreshing
the pinned dependencies resolved it; the ARM64 release APK now builds successfully
with the current Java/JNI/manifest changes. Emulator/keyboard qualification is
still pending; do not inherit it from older screenshots.

## Checkpoints

- `062029443`: selectively ported PR #198's layout foundations; did not replace
  the shared incremental mobile/browser host or import desktop GL/event-loop work.
- `009533767`: native phone HUD, readable counters, scrollable enlarged inspector,
  tutorial wrapping, minimap transform and iOS safe areas.
- `235c73c33`: large worker allocation and initial phone in-game menu.
- `433a03f08`: priority/range tabs with pending-order feedback and limits.
- **`3a3e419d3` is the previous well-verified checkpoint**: native regressions,
  desktop/Wasm/iOS simulator builds, and 27 browser viewport/settings tests passed.
- **`f69f21b42` is the broader UI work checkpoint being handed off**. It adds the
  changes below. Treat its expanded platform/UI coverage as work needing further
  qualification, rather than inheriting the older checkpoint's evidence.

## What the broader checkpoint contains

`src/gui/GameGUITouchActions.cpp`:
- An Actions tab in owned-building inspectors, with a scrollable list of large
  production ratio, clearing-resource, warrior/explorer requirement controls.
- Repair/upgrade/cancel actions routed through the existing building click handler.
- Destruction requires a second confirmation; cancel-destruction remains available.
- Pending ratio/resource/requirement values queue the original order types.

`src/gui/GameGUITouchDialogs.cpp`:
- A native phone presentation of the existing in-game dialog widgets: visible
  labels, buttons, lists, toggles, selectors, text fields and objective text.
- Scrollable content, wrapping, fixed footer actions and pointer ownership.
- In-game main menu, options, objectives, alliance, load/save, chat and history
  use this presenter. It leaves desktop/browser widget layouts intact.
- Added menu actions for pause, chat, minimap, statistics, map marks, health/food
  information, overlays and replay viewing controls. Replay toolbar offers
  pause/play and fast-forward; replay selection must remain read-only.
- The earlier bespoke five-button phone pause menu was replaced by this shared
  presenter. Older screenshots describe older commits, not the current layout.

`GameGUITouch.cpp/.h` retains camera/gesture/placement logic and adds shared text
wrapping, clip handling and gesture ownership. `GameGUIDraw.cpp` routes overlays
into the presenter. Small toolkit APIs expose widget geometry/captions/values and
activate the original widget callbacks; there is no second save or simulation path.
The SCons shared source manifest includes both new translation units.

Platform changes in this checkpoint:
- iOS `mobile/ios/SafeArea.mm` observes keyboard frame notifications; the dialog
  presenter subtracts keyboard overlap and can scroll the focused field into view.
- Android `Glob2Activity.java` publishes an immutable inset snapshot to JNI instead
  of padding the SDL surface. The HUD reserves those UI margins while keeping the
  world edge to edge. Manifest uses `adjustNothing`. **Android has not been rebuilt
  or keyboard-tested for these latest Java/JNI/manifest changes.**
- A fixed Hide keyboard action appears after activating a text field.
- New labels are in English and the key catalog; existing translations are reused
  where possible. Translation completeness/long-label layouts remain to verify.

## Verification for this checkpoint

The native gameplay-touch harness builds and passes with the broader dialog tests.
It exercises the preexisting camera/placement/allocation/priority/range tests plus
both 320×568 and 568×320 dialog flows: options/mute, objective tabs, filename text
input, Hide keyboard, save cancellation, and chat generating exactly one message
order. These are synthetic SDL touch events through the real GameGUI dispatch.

The first expanded test crashed because its LoadSaveScreen fixture omitted the
filename conversion callbacks supplied by the real gameplay flow. The fixture
now supplies `glob2FilenameToName`/`glob2NameToFilename` and passes. The backtrace is
in the ignored `build/gameplay-complete-debug.log`; it is not an unresolved crash.

Current native captures are under `build/mobile-test-profile/touch-*.bmp`.
Checked-in `gameplay-options-portrait.png` and `gameplay-save-landscape.png` are
native harness captures, **not emulator or live-match evidence**. Save capture
shows the scroll position near the filename and fixed footer.

The Wasm release and ARM64 iOS simulator application builds also pass for
`f69f21b42` (logs listed below). Final outcomes are recorded in [status](status.md).
Do not infer that the
prior browser tests or simulator launch were rerun for this broader checkpoint.
No fresh Android package, device launch, hardware keyboard test, or real-device
performance qualification is implied by the native test result.

## Resume in this order

1. Review this checkpoint's diff from `3a3e419d3`. Add actual touch-sequence tests
   for every new Actions-sheet control: all ratios/limits, clearing toggles,
   warrior/explorer requirements, repair/upgrade/cancel, destruction confirmation,
   cancellation on selection/state changes, scrolling and rotation.
2. Exercise actual save/load success, durable failure/retry, lists with many files,
   long translated labels, objective paragraphs, 16-player alliances/fixed teams,
   chat cancellation/recipients/history and end-of-game buttons. The new tests only
   cancel a save; they do not prove complete save/load qualification.
3. Test real replay playback and all viewing controls/team switches. Replay HUD
   support was enabled in this checkpoint but is not covered by a loaded-replay
   touch fixture yet. Check that no replay action can emit game-changing orders.
4. Audit in-game reachability: brush modes/sizes and cancellation, construction
   costs/upgrade previews formerly shown on hover, event/home navigation, tutorial
   highlight anchors, text/graph statistics, overlays and minimap. Do not label the
   entire UI finished merely because it has large buttons. Some legacy inspector
   controls remain below the dedicated controls.
5. Build/install current Android and iOS simulator apps and visually test touch,
   both orientations, safe areas, keyboard show/hide, focus/background and canceled
   gestures. In particular verify Android 8 IME insets with `adjustNothing`, JNI
   inset availability, and iOS keyboard notification timing/floating keyboards.
   Verify footer/input reachability while the keyboard is actually visible.
6. Run affected desktop/browser regression gates. The new native HUD still requires
   SDL's portable renderer; mobile-browser touch parity remains a separate gap.
   Check tablet/large-window handling (HUD currently selected for short dimension
   below 600 points, or `GLOB2_TOUCH_HUD=1`), translations and adjustable UI scaling.
7. Update docs, PR screenshots and evidence, keeping qualification limits explicit.
   Physical-device checks, determinism/performance gates and the larger networking/
   persistence/toolchain roadmap in status.md remain outstanding.

## Local commands and artifacts

Run these from `/Users/bradley/glob2-mobile`:

```sh
git status --short
git log -5 --oneline
scons release=1 -j8 gameplay-touch-test
GLOB2_USER_DATA_DIR="$PWD/build/mobile-test-profile" \
  ./build/darwin/client/release/src/gameplay-touch-test
scons target=web release=1 -j8
PLAYWRIGHT_BROWSERS_PATH="$PWD/build/mobile-tools/playwright" \
  TMPDIR="$PWD/build/mobile-tools/tmp" \
  browser/node_modules/.bin/playwright test -c browser/playwright.config.js \
  viewport.spec.js settings-storage.spec.js
python3 mobile/ios.py build --environment simulator --release \
  --cmake "$PWD/build/mobile-tools/downloads/tools/cmake-3.30.1-osx/cmake-3.30.1-macos-universal/CMake.app/Contents/bin/cmake"
```

More commands for Android packaging/signing/install, iOS signing/install/debugging,
and the larger native suites are in development.md. Do not run `scons install`.
Xcode is installed at `/Applications/Xcode.app` (26.6, SDK 26.5); leave the global
xcode-select choice alone. Android SDK: `build/mobile-tools/android-sdk`, NDK
28.2.13676358, Java 17 under `build/mobile-tools`. Keep all new tooling task-local.

Our isolated iOS device set is `build/mobile-tools/ios-simulators`; prior test device
Glob2-Mobile iPhone 16 has UDID `478ACBC2-34A7-4B32-AB25-0F33CA707C5E`.
Do not operate the user's separate default iPhone 17 simulator. List the custom
set's current state before boot/install. Prior work shut our device down. Earlier
cleanup removed rebuildable iOS **device** core objects/archive; the next device
build will recompile them. Preserve apps, symbols, SDKs and simulator data. Check
disk space before broad builds; archive creation needs temporary copies.

Ignored logs for this pass: `build/gameplay-complete-native.log`,
`build/gameplay-complete-touch.log`, `build/gameplay-handoff-web.log`,
`build/gameplay-handoff-ios.log`. Earlier `build/priority-merged-*` logs belong to
the previous verified checkpoint. Build logs/caches remain on this Mac and are
not part of Git; the committed documentation contains the portable handoff.
