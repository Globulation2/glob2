# Mobile verification and design gallery

Run native checks, capture consistent layouts and verify real-device behavior before release.

## Verification

```sh
python3 -m unittest discover -s test/build_system -v
scons release=1 tests
python3 test/run_tests.py --binary unit --filter 'MobileInput/*' --filter 'MobileDocuments/*'
python3 test/run_tests.py --filter 'PortableRenderer/*' --filter 'GameGUITouch/*' --filter 'UIPresentation/*'
```

The display cases need an SDL display (Xvfb on Linux, opened by the runner) and
copy their screenshots into `artifacts/tests/<suite>/<case>/`. These checks cover
build isolation, artifact
validation, rendering and input geometry; they do not replace device gameplay,
background recovery, document-picker or cross-platform simulation checks.

`mobile/ios_smoke.py` checks startup, background/resume and fresh-process relaunch
on a booted simulator named `Glob2-Mobile` in the isolated device set. Pass its
UUID with `--device`; diagnostics default to `build/mobile-smoke-ios`.
`mobile/smoke.py` and `mobile/android_trust_test.py` provide Android diagnostics;
the startup smoke selects a hand preference on fresh profiles before checking
the main menu, settings input, lifecycle and persisted profile. Their UI sequences
may require updating as the shared interface evolves.
Keep screenshots and logs under ignored `build/` or `artifacts/` directories.
Native document picker and certificate
trust adapters are selected only for mobile builds. Simulation, replay and save
compatibility must be checked with the shared replay-verification workflow before
shipping a mobile client.


## Focused iOS localization qualification

The existing `ci-macos.yml` manual workflow offers `ios_localization=true` and
`locale=th` for a committed revision. This selects an unsigned simulator build
and lifecycle smoke rather than the normal macOS native job. Workflow calls and
ordinary manual runs retain native coverage. The job validates the existing
pinned Xcode, reuses content-validated simulator dependencies, checks the actual
Thai catalog/font/notices and localized permission-purpose string in the built
bundle, and retains app symbols, logs, hashes and screenshots as an artifact.
It uses no Apple signing credentials or release operations.

The fixture marks the initial hand choice and introduction as offered and forces
Thai through `SDL_PREFERRED_LOCALES` plus the simulator language defaults. It
leaves the saved game language unset, so startup exercises locale selection.
Review the actual screenshots before accepting visual quality. This fixture
establishes simulator startup, background/resume and fresh-process relaunch;
it does not establish the OS permission popup, Thai IME interaction or physical
device behavior. The helper only shuts down and deletes its recorded private
simulator. Build and platform failures retain their actual diagnostics.

The existing `mobile.yml` workflow also accepts the optional manual input
`android_localization=true` with `locale=th`. That mode builds and tests x86_64
only, verifies the actual APK catalog/font/notices and Thai Android startup
resource, and selects Thai with Android's per-app locale API before the existing
startup/settings/rotation/lifecycle smoke. Ordinary runs and workflow calls keep
their original architecture selection. Review retained screenshots for actual
Thai rendering; the transient unpack label is checked in compiled resources,
not guaranteed to appear in the screenshots. Physical devices and Thai IME
interaction remain separate checks.

## Mobile design review gallery

The offline gallery captures production screen classes at ten logical viewport
sizes: 320×568, 568×320, 390×844, 844×390, 768×1024 and 1024×768 in Compact
mode (with adaptive frontend tablet composition), tablet Automatic at 768×1024,
tablet Spacious at 1024×768, plus 1280×800 and 1920×1080 with desktop mouse controls. It includes
named menu, settings, setup, gameplay, dialog and editor states. The gallery lets
reviewers compare any two sizes, inspect full-resolution images, search by screen
name or stable ID, and record/export/import feedback per view.

```sh
scons release=1 mobile-gallery
python3 tools/mobile_gallery/capture.py
# Faster in-game-only iteration, with the same IDs and comparison sizes:
python3 tools/mobile_gallery/capture.py --game-only --output artifacts/mobile-gallery/gameplay-review
# Editor-only comparison, including map creation and campaign forms:
python3 tools/mobile_gallery/capture.py --editor-only --output artifacts/mobile-gallery/editor-review
open artifacts/mobile-gallery/review/index.html
```

The Python runner requires Pillow. Pass `--binary` for another host build path
and `--output artifacts/mobile-gallery/another-review` for a new capture. Output
profiles must be fresh: the runner refuses to combine existing profiles with a
new capture. `--reuse` rebuilds the gallery from existing raw captures after
editing its HTML, CSS, JavaScript or catalog; it does not update the screenshots.
Use `--extend --output EXISTING_GALLERY` to add newly registered sizes without
recapturing the existing ones. Existing image hashes are checked before reuse,
and the manifest records revision, binary hash and source evidence per size.
The gallery works offline without a web server. Browser storage retains notes
where available; **Export feedback** creates the portable JSON copy to share or
keep. Import merges nonempty review entries by stable screen ID.

`tools/MobileGalleryHarness.cpp` selects fixture states and renders through the
normal screen stack, `GameGUI` and `MapEdit`. It uses bundled maps/campaigns,
a fixed generated-map seed, and a disposable profile for each viewport. The gameplay fixture advances 4,096 simulation ticks, populates three named
players and authored objectives, and records/loads a replay in its disposable
profile. No chat submission, account creation, uploads or personal saves occur. Screen fixtures may select internal state directly; this is a visual
capture tool, not proof that every state is reachable through touch navigation.
Existing interaction harnesses remain separate. The gallery also records native SDL building drags and
unfinished zone strokes, and exposes tactical tools, replay controls, open team
filters and chart-value inspection as separate review states. The decorative
menu colony keeps advancing under the menus.

Generated preview captures wait for both worker completion and the real-time
crossfade. Landscape captures wait for visible cards to settle; their offscreen
cards do not need to animate before capture. Waits are bounded and fail the run
rather than silently presenting an unfinished preview as the ready state.
Loading and error preview fixtures have separate, explicitly named views.

The runner selects SDL's dummy video driver and software renderer to avoid the
host window manager constraining tall phone windows. Screenshots come from the
renderer **before presentation**, which clears the backbuffer. They record the
final viewport, including any legacy scaling or letterboxing, rather than merely
copying the logical drawing surface. The runner rejects missing, undocumented,
blank or incorrectly sized images. It retains raw BMPs, PNGs, capture logs,
source revision/diff, tool sources and image/binary hashes under the output
folder for review and reproducibility.

These are **native host captures**: Compact on phones/tablets, Automatic/Spacious touch on tablets, and desktop mouse
presentation on desktop sizes. Desktop captures set the logical canvas to the
requested dimensions, rather than magnifying an 800×600 surface. Historical inspector IDs now show the unified touch inspector and preserve
feedback. Desktop retains its original sidebar, so repeated desktop images for
those states are intentional. They are not Android/iOS device screenshots. Desktop-only settings are omitted
on phone/tablet; retired views retain their feedback IDs and show an explicit
unavailable notice. Native keyboards, safe areas, document pickers and lifecycle
behavior still require simulator/device checks.
The gallery's coverage panel explicitly lists connected multiplayer and remaining state variants that are not captured.
Do not interpret the screenshot count as exhaustive coverage of all UI states.

To add a view, give it a stable capture name in the relevant C++ screen-family
function and an entry in `tools/mobile_gallery/catalog.json`, including its
fixture context. Optional `availability` values (`phone`, `mobile`, `desktop`,
`retired`) explicitly identify views that should not have an image at every size. Capture the actual production screen; do not redraw or repair
its appearance in the gallery. Add new sizes to the runner's `SIZES` matrix.
Map fixtures must select a known premade map or fixed-seed generated map and wait
for actual preview pixels and the fade to settle. Loading/error states discard
preview pixels: restore the snapshot, not just the Ready flag, before capturing
subsequent pages. Pass production colony names/colors into quality-report fixtures.
Keep screenshots, profiles, feedback and review notes in `artifacts/`; maintain
only the reusable tool and its documentation in Git.


## Secure multiplayer and LAN discovery

Multiplayer uses native WSS listeners and verified TLS on desktop and mobile.
LAN hosts share a session certificate fingerprint in the waiting room; guests
paste that pairing string before exchanging game details. See
[secure multiplayer](../browser/gateway.md) for trust, framing, and compatibility.

The iOS project includes `mobile/ios/Glob2.entitlements` for UDP broadcast LAN
discovery. Device signing requires a provisioning profile approved for Apple's
multicast entitlement, in addition to the existing local-network usage
explanation. Manual pairing remains available when discovery cannot run.
Android currently targets API 36 and uses INTERNET for LAN and online connections. A future
API 37 target must add ACCESS_LOCAL_NETWORK and request it before LAN access,
as described by [Android's local-network permission guide](https://developer.android.com/privacy-and-security/local-network-permission).
Connection failures point players to local-network permission and certificate
pairing rather than falling back to plaintext.

[Mobile index](README.md) · [Documentation index](../README.md).
