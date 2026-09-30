# Mobile builds and interface

Android and iOS use the shared game sources and SDL renderer. The mobile targets
have isolated toolchains, dependency archives and output directories; they do not
use host libraries or install into the desktop application's directories.

The phone presentation shares simulation, game orders, settings persistence and
lobby setup with desktop. `InterfacePresentation.h` selects the presentation;
`GameGUITouch` owns gameplay gestures and phone panels. Menus and dialogs are
element trees on the declarative UI framework (see the
[UI framework guide](../development/ui-framework.md)), which adapts one build
path per screen to phones, tablets and desktops. Automatic presentation uses available logical space and touch capability
on every host. Settings offers Automatic, Compact and Spacious. Spacious requires
480 points of map width beside the panel and 480 points of usable height;
the panel is 288 points for touch and the existing 160 points for mouse controls.
Spacious falls back to Compact when it cannot fit. Safe areas and interface scale
participate in fit; the onscreen keyboard only reduces dialog space.
Touch-capable hosts use targets at least 48 points tall. Pointer availability
controls hover hints without switching layouts. Resize preserves the camera,
selection, tool, panel, scroll position and text while cancelling held input.

`GLOB2_MOBILE_UI=1` forces the compact presentation for development; `0` forces
legacy controls. `touch-auto` and `touch-spacious` exercise touch presentation
with Automatic and Spacious layout policies on a native development host. This is
the single development override; leave it unset for normal saved preferences and
host capabilities. Rendering backend and mouse
motion do not select a presentation. SDL, OpenGL/WebGL and software rendering
share UI transforms and clipping. Mouse clicks use the same controls as touch;
Tab and Enter navigate adapted forms, and Page Up/Down scroll gameplay panels.
Mobile settings omit desktop window sizes, renderer selection and OpenGL-only
options because the operating system manages the viewport and the mobile build
uses the portable renderer.

The gameplay toolbar opens build choices, flags/zones, tactical tools, objectives,
alliances and the session menu. The minimap is a separate top-right HUD component;
phone palettes float over the camera, while spacious touch layouts keep a
content-sized palette open at the right. Both preserve the camera framing and
leave the world visible below short panels. In-game surfaces use `InGameTouchTheme.h`; frontend paper styling remains
independent. A completed tap on empty map space dismisses building inspection
and restores the previous palette state. Tapping another object switches selection;
panning, cancelled gestures and taps inside the inspector do not dismiss it.
Two quick taps on the same map position restore 1:1 zoom around that point;
pinching still adjusts zoom continuously. Touches on controls do not reset zoom.
In the mobile/touch interface, flags on the flat map also accept selection within 24 screen points of their
centres, independent of zoom. Exact flag hits retain priority; the extra halo
does not override direct unit/building hits and chooses the nearest flag.
Halo clicks select without moving the flag; direct flag grabs retain dragging.
Desktop mouse selection retains its original exact-tile hit area.
Flags and zones share one fitted row. Inspectors group production ratios side by
side and use horizontal room for worker/priority and action controls. Ordinary
phone inspectors fit without scrolling; overflow remains available for constrained
safe areas and enlarged interfaces. Drawing, hit testing and slider geometry use
the same action boxes.

### Safe areas

Interactive content uses the host safe rectangle, including Android system bars
and display cutouts and iOS safe-area insets. Backgrounds may extend edge to edge.
The framework's `Presentation::safe` and `Presentation::dialog` rectangles,
resolved from `mobileDialogSafe`, bound menus, footers, scroll viewports and
popups; full window dimensions are only appropriate for backgrounds and
pointer-coordinate conversion. Gameplay reserves
system insets separately from keyboard occlusion so the camera stays stable.

Insets can change without a window resize. The screen stack refreshes host metrics,
cancels held input and invalidates layout when this happens. The UI presentation
harness injects host-point gutters through `mobileSafeInsetsForTesting` and checks
every screen and dialog fixture against them on phones, tablets and desktops.
Keep this override unset outside tests. Device checks
must also cover Android gesture/three-button navigation and rotation.

### Gameplay responsibilities and action flow

- `GameGUITouch` composes explicit bounds, routes input ownership, presents the HUD,
  and restores the previous palette after inspection. It never draws the desktop
  sidebar or forwards touch controls to its pixel hit tests.
- `GameGUITouchPalette.cpp` reads available building/flag choices and draws artwork
  in a content-sized grid. Zone entries enter painting mode instead of placement.
- `GameGUITouchView.cpp` draws independently bounded HUD components, the minimap,
  tutorial, tactical panel and contextual headers. It shares primitives, not the
  desktop sidebar composition.
- `TouchInteractionSession.h` holds pointer identity, the originating palette,
  preview ownership, and buffered world-space stroke points. It contains no artwork,
  layout rules, or simulation mutations.
- `GameGUITouchPlacement.cpp` interprets palette taps and drags. Both commit through
  `GameGUIToolManager::confirmBuilding`, including its existing validation, defaults,
  ghost suppression, and order serialization. A valid drag release places once and
  restores the palette; a tap selects a movable preview with Confirm/Cancel.
  Both patterns pan continuously while the owning finger stays near an exposed
  map edge. Speed is density- and zoom-aware, and the preview follows the camera.
  Release stops preview-mode panning without committing; a second contact,
  focus loss, rotation or selection change cancels the edge-pan contact.
  UI-covered edges do not pan.
- `GameGUITouchActions.cpp` presents the selected building identity and actions,
  reading pending values through `GameGUI::displayed*` and using shared request
  methods for allocation, priority, range, construction and destruction. Enemy
  and replay selections are read-only. Specialized controls share the same panel.
  Worker-slider drags own a local allocation session and emit one command on release;
  a second contact, selection change, focus loss or rotation cancels the preview.
- In-game dialogs (`GameGUIDialog.cpp`, `LoadSaveDialog.cpp` and the message
  history) are framework dialogs hosted by `GameGUI` on every presentation;
  touch and desktop render the same tree in the in-match theme. File operations
  keep their existing persistence and error/retry state machines.
- `EndGameScreen` owns a chart, metric dropdown, team filters, expansion and replay
  export on both desktop and touch. `EndGameStat` retains history interpretation,
  including explanations for missing measurement coverage. Compact layouts put metric
  and team-filter entry points in one row, with scrollable filters over the plot.
  Axis labels stay outside the curves.

A session cannot commit after a second finger, focus loss, rotation, selection
change, or release over UI. Drag previews are lifted above the finger; edge panning
continues while held. Tap previews survive ordinary input suspension and require a
new confirmation gesture. Painting buffers an unfinished stroke; release applies
its existing brush operations, while interruption discards it. Completed strokes
are never undone by leaving the tool. Two fingers navigate instead of painting.

To add another interaction pattern, make it update the owned preview and call the
same commit operation. Keep gesture thresholds and sizing policies in the in-game
theme, add cancellation/order-equivalence cases to `GameGUITouchHarness`, and
capture the result through the production renderer. Do not duplicate construction
rules or adapt another presentation's composed screen.

### Map and campaign editor

`PhoneEditor` owns the touch editor's map bounds, compact header, bottom mode
strip and horizontally scrolling artwork palette. Terrain and Resources share
brush operations; Buildings and Flags expose team and level beside the map.
Individual artwork widgets are reused, never the composed desktop sidebar or
its minimap. Done leaves the active tool and returns to object selection; Pan
switches one-finger navigation. Brush opens a visual mask chooser. Pending
strokes draw their coverage before release without changing the map. Presentation measurements and drag thresholds are point-based
policies at the top of `PhoneEditor.cpp`.

A palette drag owns one pointer. Moving into the map lifts a placement preview;
release calls the existing named `place building` or `place unit` action once.
Horizontal movement within the tray browses choices. A tap selects the tool for
subsequent map taps. Brush strokes buffer points until release, then invoke the
existing editor brush actions. Leaving the map, losing focus, resizing or adding
a second finger cancels unfinished work. Two fingers navigate without painting.
`PhoneEditorView.cpp` renders pending gestures and contextual object inspectors.
Inspector rows bind a named property to its value and step controls; closing an
inspector restores the palette. `ValueScrollBox::setValue` invokes the existing
semantic action without desktop hit-test coordinates.
These gestures do not change map serialization or construction rules. New input
patterns should feed the same placement/brush operations and explicitly define
pointer ownership and cancellation.

Script, briefing and hints use the framework text editor with a separate IME
composition buffer; when the keyboard leaves little vertical space the action
row folds into the scrolling body without losing the draft. `ScriptEditorScreen`
composes tabs, editor and command bars; `TeamsEditor` composes rows and real
color swatches; `MapEditMenuScreen` is a bounded session menu. Save and load use
`LoadSaveDialog`, which renders the `FilePresentation` model including busy,
retry and export states and also serves child script-file dialogs. Area naming
uses `AskForTextInput` with native UTF-8 input. IME preedit is displayed
separately; only explicit confirmation commits the draft, and cancellation
retains the original name. `MapEdit` hosts these dialogs on desktop and touch
alike.

`NewMapScreen` presents Blank/Generated choices and shares the production
landscape browser with Custom Game. A chosen landscape retains its explicit seed
and generation parameters. Campaign views and map loading lay out preview and
details through the framework.
Campaign descriptions own touch cursor placement, swipe scrolling and provisional
IME text. Campaign lists distinguish completed taps from scrolls. Narrow script
entry navigation uses large Previous/entry/Next controls with stable script IDs;
multiple contacts cancel pending actions.
The editor menu uses the colony background and omits the main-menu logo. Native
lists opt into a shared minimum touch-row height so painting, hit testing and
scrolling use the same geometry; desktop retains its default row sizing.

Voice capture is disabled on mobile. Native phone-size checks do not establish
Android/iOS release readiness; real keyboard composition, safe areas, lifecycle,
and phone/tablet play sessions remain necessary.

## Toolchains and output isolation

Run commands from the repository root. Versions and checksums are pinned in
`mobile/toolchain.json`, `mobile/android-tools.json` and `mobile/vcpkg.json`.

Android CI opts into the shared `CCACHE=1` compiler wrapper and restores a
per-architecture cache keyed by the pinned toolchain, dependency manifest and
triplets. A cache miss still builds normally; ccache checks compiler content and
source dependencies before reusing an object. Leave `CCACHE` unset when generating
`compile_commands.json` for source analysis. A newer PR push cancels its older
Android checks.

Use `python3 mobile/doctor.py android` or `python3 mobile/doctor.py ios` to inspect
SDK availability. iOS requires the pinned full Xcode 27.0 toolchain;
`--developer-dir` on the packaging command selects it without changing the
machine's Xcode selection.

Build outputs live under
`build/<android|ios>/<device|simulator>/<arch>/<api>/<client>/<debug|release>`.
Dependency manifests bind archive hashes to the target and compiler. SDKs,
emulators, generated projects, signing keys and caches stay under `build/`.

### Text raster resolution

Screen text uses the drawable pixel scale multiplied by the active UI transform,
including the SDL portable renderer on Android/iOS. Font layout measurements stay
at their authored size; only glyph rasterization changes. Offscreen surfaces keep
logical-resolution text because they cannot retain extra screen pixels. Raster
fonts and string caches are keyed by integer font size so alternating HUD/control
scales do not reopen fonts or discard other labels each frame. Verify sharpness
on device screenshots: a density-1 emulator can hide an enlarged-glyph defect.
`GameGUITouchHarness` checks first-draw resolution, stable metrics, offscreen text
and cache reuse under alternating transforms.

## App icons

Both mobile packages use the canonical blue Glob artwork from
`data/icons/glob2-icon-128x128.png`, on the in-game purple background. Android
ships density-specific legacy icons and a padded adaptive foreground; iOS ships
an opaque iPhone/iPad AppIcon asset catalog, compiled by Xcode into the bundle.
The canonical source is 128 pixels; larger sizes are resampled from that source.

The generated assets are checked in, so normal builds need no image tooling.
After changing the canonical source, run `python3 mobile/icons.py` with Pillow
installed and review both the adaptive foreground and opaque icon before committing.

## Android

Install Python, SCons, Git, make, autotools and pkg-config. Bootstrap the pinned
NDK and packaging tools, then build target dependencies and stage the project:

```sh
python3 mobile/setup_ndk.py
python3 mobile/setup_tools.py
python3 mobile/dependencies.py --arch arm64-v8a --release
python3 mobile/android.py configure --arch arm64-v8a --release
```

Install the Android SDK platform and build-tools versions listed in the toolchain
manifest using `sdkmanager`, accepting the SDK license yourself. Then package:

```sh
python3 mobile/android.py build --arch arm64-v8a --release
python3 mobile/android.py sign --arch arm64-v8a --release
python3 mobile/android.py install --arch arm64-v8a --release --serial DEVICE_SERIAL
python3 mobile/android.py launch --arch arm64-v8a --release --serial DEVICE_SERIAL
```

Release builds start unsigned. `sign` uses a local developer key and validates
ZIP alignment, the signature and APK digest. Store distribution requires separate
signing arrangements. Use `armeabi-v7a` or `x86_64` for other supported targets.
The F-Droid build mode adds `--fdroid` to the Android build, sign, install, and
launch commands. Both F-Droid and Play use `org.globulation2.glob2`. The
F-Droid release uses
`mobile/android-release.json` for its static version
name and base code. The three single-ABI APKs have codes `10 * base + 1` for
`armeabi-v7a`, `+ 2` for `arm64-v8a`, and `+ 3` for `x86_64`. `build --fdroid` checks the
APK's package name, version, ABI, alignment, native symbols, and indexed assets.
`mobile/android_release.py check` verifies the release manifest against the
desktop package version. The signed output of `sign` is for development and
device testing; F-Droid signs its own published APKs.
The two stores use different signing keys, so switching stores requires
uninstalling the existing app and backing up or exporting saves first.
Omit `--release` consistently from both dependency and application commands for
debug builds. An explicit `JAVA_HOME` takes precedence over the task-local JDK.
Release Android packages use the source package version as `versionName` by
default; pass `--version-name` to choose a different store-facing value.
The `--version-code` argument is carried into the generated Gradle project.
For the separate mainland China local-play configuration, pass `--china` to
both the dependency and Android packaging commands. The iOS dependency and
packaging commands accept the same flag.

The asset packager excludes local caches and metadata before writing its index.
The completed APK is checked against that index and its content digest, so an
AAPT-filtered or missing file fails the build instead of failing on first launch.
Release packaging restores gzip assets that AAPT expands and renames, then aligns
the APK before signing so compressed maps retain their indexed paths.
Native startup failures are also written to Android logcat under `SDL/APP`.

### Amazon Appstore Fire tablet release

The Amazon candidate is one release APK with `arm64-v8a` and `armeabi-v7a`
libraries. It keeps Android API 26 as its minimum, so the first release can
target qualified Fire OS 7/8 tablets, not Fire OS 5/6. Build both dependency
sets, then package the APK from the arm64 configuration:

```sh
python3 mobile/dependencies.py --arch arm64-v8a --release
python3 mobile/dependencies.py --arch armeabi-v7a --release
python3 mobile/android.py build --arch arm64-v8a --release --amazon-apk \
  --version-code "$(python3 mobile/amazon_release.py version-code)"
```

Fire OS 5/6 support is a separate release milestone: lower the Android API
floor only after updating the build identity and native dependency triplets,
auditing platform API use, and playing on representative older tablets. Do not
select those devices in the store until they pass.

`--amazon-apk` builds an isolated Amazon native flavor that hides and blocks
the public YOG account flow while retaining LAN play. It reuses the standard
Android dependency builds and checks matching native libraries in both ABIs,
the packaged asset index, alignment and both native build IDs. It derives
`versionName` from
`PACKAGE_VERSION` in `scons/build_layout.py`. The four version components map
to one increasing Android `versionCode`; never reuse or lower a code already
submitted to Amazon. A release build remains unsigned until the separate
release workflow signs it. The local `sign` command above is for developer
installs and must not be used as a store identity.

The public `.github/workflows/amazon-appstore.yml` runs only when mirrored to
`genixpro/glob2-release`; its environment secrets remain private.
Its manual dispatch selects a public `vVERSION` tag that resolves to
the same commit in the release mirror. `build` produces a
verified unsigned APK without credentials. `candidate` signs it and retains a
short-lived APK for the first manual console submission. `publish` performs the
same build and signing, then submits an update through Amazon's App Submission
API. The workflow does not create the first listing. Protect the release
repository's `master` branch and release tags in both repositories; restrict dispatch and
environment access to maintainers. Configure these private environment values:

After the public change and its release tag are reviewed and merged, fetch the
public tag into the release mirror and push that exact tag ref there. The
selected tag must be reachable from public `master` and point to the same
source commit in both repositories. Do not recreate it on the mirror's merge
commit. Dispatch the workflow from mirror `master` only after that merge is
present there.

| Name | Type | Purpose |
| --- | --- | --- |
| `GLOB2_AMAZON_KEYSTORE_BASE64` | secret | Base64 PKCS#12 upload keystore |
| `GLOB2_AMAZON_STORE_PASSWORD`, `GLOB2_AMAZON_KEY_PASSWORD` | secrets | Upload-key passwords |
| `GLOB2_AMAZON_CLIENT_ID`, `GLOB2_AMAZON_CLIENT_SECRET` | secrets | App Submission API security profile |
| `GLOB2_AMAZON_APP_ID` | variable | App ID from the Developer Console |
| `GLOB2_AMAZON_CERT_SHA256` | variable | SHA-256 digest of the upload certificate |

Generate the upload keystore on a trusted machine with `keytool -genkeypair
-storetype PKCS12 -keystore glob2-amazon.p12 -alias glob2-amazon -keyalg RSA
-keysize 3072 -validity 10000`; enter passwords interactively. Record its
certificate SHA-256 from `keytool -list -v -keystore glob2-amazon.p12` as 64
hex digits without colons, back up
the keystore securely, and encode it for the environment secret without adding
it to Git or workflow logs. In the Amazon Developer Console, create an App
Submission API security profile and attach it to that API before storing its
client ID and secret. The App ID exists only after the first app is created.

Register an Amazon Developer account, complete identity checks, and create the
first app version in the Developer Console. Use package `org.globulation2.glob2`,
price Free, and disable optional Amazon DRM. Select only Fire tablets that pass
qualification. Complete the privacy questionnaire based on the actual network
and account behavior, link the [Fire tablet privacy policy](amazon-privacy-policy.md),
and supply a support contact, icon, and Fire-device
screenshots. Draft listing copy: **Globulation 2** — “Build and guide a colony in
an open-source real-time strategy game. Set priorities for your workers, gather
resources, construct buildings, explore maps, and compete with other colonies.”
Review this copy against the final Fire build and Amazon's listing fields before
submission. Amazon requires the first version through the console; the API can
submit later versions. Keep the first signed APK and its certificate identity
for future update verification.

Before the first submission, play the signed APK on a Fire OS 7 and a Fire OS 8
tablet, including a 32-bit device where relevant. Check install, start, an
entire match, touch placement and painting, keyboard/IME entry, audio, rotation,
background/resume, save/load and network play. Record device models, Fire OS
versions, screenshots, logs, saves and replay/checksum evidence under
`artifacts/`, and inspect the supported-device list in the Developer Console.
After the first version is live, run one controlled API update and check that a
store-delivered update retains local saves. API review status and rejected
submissions require console follow-up; the workflow deliberately leaves an
open edit untouched if a submission fails.

### Google Play internal testing

The Play app ID is `org.globulation2.glob2`. The Play upload is a release Android
App Bundle. Install Android SDK platform 36
alongside the pinned NDK and build tools. The existing APK commands above remain
useful for direct device testing. To build the bundle:

```sh
python3 mobile/android.py bundle --arch arm64-v8a --release --version-code 1
```

Each subsequent Play upload needs a higher `--version-code`. The bundle command
restores gzip assets that Android packaging expands, verifies the complete game
asset index, and checks the packaged native build ID against retained symbols.
The unsigned bundle is written to
`build/android/device/arm64-v8a/26/client/release/android-project/app/build/outputs/bundle/release/app-release.aab`.
Keep a private upload keystore outside the repository and back it up securely.
Create the key with Android Studio's **Generate Signed Bundle/APK** flow or
`keytool`. Set `GLOB2_PLAY_STORE_PASSWORD` and `GLOB2_PLAY_KEY_PASSWORD` in the
environment using your password manager, then sign the verified bundle:

```sh
python3 mobile/android.py sign-bundle --arch arm64-v8a --release \
  --keystore /path/to/private-upload.keystore --key-alias YOUR_ALIAS
```

Upload `app-release-play.aab` from the same directory to Play Console's **Internal
testing** track. The development APK key must not be used as the Play upload key.
Before uploading, use bundletool to generate and install APKs from the bundle,
verify its `PAGE_ALIGNMENT_16K` setting, and check startup, gameplay, rotation,
background/resume and save/load on a real device. Keep the generated `.apks`,
screenshots, logs and replay checksums under `artifacts/`.

### Automated Google Play internal releases

`.github/workflows/android-play-internal.yml` is public for review, but its
release job runs only from the owner's public `genixpro/glob2-release` mirror on
`master` after a manual dispatch. The mirror has owner-only write access and
hosts the other platform release workflows. Copy reviewed upstream commits into
the mirror when ready,
then manually dispatch and approve its workflow. An unprivileged
job installs the pinned Android toolchain and dependencies, assigns a
time-based Play version code, and builds and verifies the arm64 release bundle.
A fresh runner receives the unsigned bundle, signs it with the existing Play
upload key, and uses the Google Play Developer API to validate and commit a
release on the **internal** track. It does not
change production or the selected internal tester list. The workflow is
serialized so two runs cannot update the track concurrently. A rerun gets a
new version code; Play rejects a code lower than a previously uploaded one.

One-time account setup is required before the first workflow run:

1. In a Google Cloud project, enable the **Google Play Android Developer API**.
   Create a service account dedicated to Globulation 2 internal releases.
2. Configure [GitHub Workload Identity Federation](https://github.com/google-github-actions/auth#workload-identity-federation-through-a-service-account)
   for that service account. Restrict the provider to the mirror's numeric
   repository ID, the owner's numeric actor ID, the `workflow_dispatch` event,
   public visibility, the `google-play-internal` environment, GitHub-hosted runners,
   `refs/heads/master`, and
   `genixpro/glob2-release/.github/workflows/android-play-internal.yml`.
   Grant only that repository identity `roles/iam.workloadIdentityUser` on the
   service account. Do not create a Google service account key for this workflow.
3. In Play Console **Users and permissions**, invite the service account's email
   address and grant app-level **View app information (read-only)** and
   **Release apps to testing tracks** for `org.globulation2.glob2` only. Do not
   grant production release or account-wide permissions.
4. In the **release mirror only**, create a GitHub Actions environment named
   `google-play-internal`, restrict deployment to `master`, require the owner to
   approve each run, and disallow administrator bypass. Add environment variables
   `GLOB2_PLAY_WIF_PROVIDER` (the full provider resource name, with numeric
   project number) and `GLOB2_PLAY_SERVICE_ACCOUNT` (the service account email).
   Add three environment secrets:

   | Secret | Value |
   | --- | --- |
   | `GLOB2_PLAY_UPLOAD_KEYSTORE_BASE64` | Base64 of the backed-up `glob2-upload.p12` used for the first Play release |
   | `GLOB2_PLAY_STORE_PASSWORD` | Upload keystore password |
   | `GLOB2_PLAY_KEY_PASSWORD` | Upload key password |

Keep the original keystore and passwords backed up outside GitHub. The public
upstream must never receive these secrets or a broad OIDC trust binding. The workflow
decodes the key to a temporary runner file, signs the AAB, removes that file,
then authenticates to Play through short-lived GitHub OIDC credentials. Only
the mirror's owner should have write access. Do not add pull-request
triggers or allow arbitrary refs to reach the release job. Review upstream
changes, especially the workflow, build scripts, and dependency pins, before
syncing them into the mirror: copied code runs with release credentials.
Public Actions logs and artifacts are visible to everyone. The unsigned bundle
is retained as an artifact for one day to cross the runner boundary; the signed
AAB is never uploaded as an artifact. The workflow does not cache build outputs.
GitHub Actions secret redaction is not a substitute for keeping secrets out of
logs and artifacts.
The Play API client and its transitive Python packages are locked to reviewed
wheel hashes in `mobile/play-api-requirements.txt`; update the lock deliberately
when upgrading them.

On the mirror, keep the owner as the sole collaborator, protect `master` against
force pushes and deletion, and require approval from the owner for the
`google-play-internal` environment without administrator bypass. Keep the
default workflow token read-only, require full commit SHA pins for release
actions, and require approval before any external fork PR workflow runs.
Review every enabled platform workflow and its environment before syncing
upstream changes. The owner's GitHub account remains a critical trust boundary:
protect it with strong two-factor authentication and review every upstream
commit before syncing code that a release build will execute.

To sync from a local clone of the release mirror, configure `upstream` once as
`https://github.com/Globulation2/glob2.git`, then use:

```sh
git fetch upstream master
git checkout master
git merge upstream/master
git push origin master
```

The release mirror has release-specific commits, so merging may require
conflict resolution. Inspect the commits and resulting tree before pushing.
Do not force-push a release branch.

After syncing, run **Actions → Android Play internal release → Run workflow**
in the mirror. The optional release notes are shown to internal testers. The
run summary records the version code and signed AAB SHA-256; only Play receives
the signed bundle. Check the internal testing track in Play Console for
availability; if Play requires a new content or policy
declaration, complete that in Play Console before rerunning the workflow.

### Native tests on a connected Android device

After configuring the release project above, build the isolated CppUnit dependency
and cross-compile the unit suite and client integration harnesses:

```sh
python3 mobile/dependencies.py --arch arm64-v8a --release --tests
scons target=android arch=arm64-v8a release=1 android-tests android-unit-tests -j4
python3 mobile/android_device_tests.py --serial DEVICE_SERIAL \
  --android-sdk "$ANDROID_SDK_ROOT" --output artifacts/android/device-tests
```

For an SDK outside the default task-local directory, pass `--android-sdk PATH`
to the Python build commands and `android_sdk=PATH` to SCons. Keep the dependency,
project and test architecture consistent. `--harness NAME` runs a selected suite.
The runner checks the device ABI, stages stripped binaries and packaged assets in
an isolated `/data/local/tmp` directory, and records each exit code and log. It
retains those fixtures for diagnosis and never clears the installed app's data.

The CppUnit source list is shared with the host suite. Integration harnesses reuse
the production client objects. `mobile/NativeTestMain.cpp` supplies only the
shell platform bridges: filesystem assets, SDL main readiness, absent Activity,
and unavailable audio. SDL dummy video runs without a Java UI. These tests execute
on the device CPU but do **not** establish real rendering, audio, native keyboard,
IME composition, lifecycle or physical gesture comfort. Test those separately in
the installed APK, including rotation, background/resume, save/load and editor
text entry. Keep device screenshots and logs with the test results.

## iOS

Build dependencies and the generated Xcode project with the same configuration:

```sh
python3 mobile/dependencies.py --target ios --environment simulator --release
python3 mobile/ios.py build --environment simulator --release
python3 mobile/ios.py install --environment simulator --release --device SIMULATOR_UUID
python3 mobile/ios.py launch --environment simulator --release --device SIMULATOR_UUID
```

The simulator tools use an isolated device set under
`build/mobile-tools/ios-simulators`. Device builds use `--environment device` and
require either `--team TEAM_ID` with local provisioning or `--unsigned` for a
compile-only build. An unsigned device app cannot be installed. Release symbols
are retained alongside the application. With `--team`, Xcode must have the Apple
Developer account added in Settings > Accounts; the build allows Xcode to create
or update the provisioning profile for the bundle ID. The generated target is
included in Xcode archives for TestFlight distribution.

### TestFlight upload

`.github/workflows/ios-testflight.yml` is kept in the public source repository,
but its upload job runs only when the owner manually dispatches it from `master`
in `genixpro/glob2-release`. The release mirror is public for free hosted Actions
runners; only its owner has write access. The job checks the mirror's numeric
repository ID, owner's actor ID, dispatch event and branch. The owner syncs
reviewed public source to the mirror and chooses when to run it. Dispatches in
`Globulation2/glob2` skip the job. The mirror uses the Xcode 27 runner
and the registered `org.globulation2.glob2` App ID on
team `CL2MNNYQX3`. Each run builds pinned iOS dependencies from source, compiles
the game, archives the iPhone app, checks the bundle ID and build number, retains
matching dSYMs, exports and validates an App Store signed IPA, and uploads a
TestFlight build. Choose **internal** for an internal-only build, or **external**
for a build that can be submitted to external TestFlight review. Distribution
signing occurs during export, so archiving does not require a registered test
device. Its build
number is `1,000,000 + 100 × GITHUB_RUN_NUMBER + GITHUB_RUN_ATTEMPT`, so mirror
runs and reruns get unique numbers after earlier public test uploads.
An internal-only build cannot be submitted for external testing or App Store
release. The external choice permits external testing after Apple's beta review;
it does not publish the app to the App Store.

One-time mirror setup requires an App Store Connect **team** API key with
permissions to manage signing assets and upload builds. Individual API keys cannot
access provisioning endpoints. In the **release mirror**, create an
`ios-testflight` environment restricted to the `master` branch and store the key
ID, issuer ID and single-line Base64 encoding of the downloaded `.p8` private
key there as environment secrets named `IOS_ASC_KEY_ID`, `IOS_ASC_ISSUER_ID` and
`IOS_ASC_KEY_P8_BASE64`. Do not place release credentials in the upstream
repository, repository-wide Actions secrets, or source code. Anyone can read
the public mirror's workflow, logs and artifacts, so none may contain secrets
or signed upload packages. The workflow writes the key
only to the ephemeral mirror runner, outside the checked-out repository.
Configure the app's internal TestFlight group for automatic distribution in App
Store Connect if testers should receive every processed build without another
manual step.

The iOS Info.plist declares `ITSAppUsesNonExemptEncryption = NO` for the app's
standard TLS use. This is the owner's export-compliance determination; revisit it
if the app's encryption changes. The workflow uploads an `.xcarchive` artifact
for diagnosis. A successful upload means Apple accepted the transfer; Apple
processes the build afterward. Check the TestFlight build status in App Store
Connect before expecting testers to install it.

For a family tester without App Store Connect account access, dispatch an
**external** build from the release mirror. In App Store Connect, complete the
beta app description, feedback contact and test information, create an external
tester group, add the processed build and submit it for TestFlight App Review.
After Apple approves the build, enable a public invitation link for that group
and share it with the tester. Keep its tester limit small and turn off the link
when it is no longer needed; anyone with the link can request access while it is
enabled. Install TestFlight on the iPhone, open the link and accept the invitation.
Play a real device session before treating the build as release ready. Keep the
App Store release step separate.

## Verification

```sh
python3 -m unittest discover -s tests/build_system -v
scons release=1 portable-renderer-test mobile-input-test gameplay-touch-test ui-presentation-test
build/darwin/client/release/libgag/src/MobileInputHarness
build/darwin/client/release/libgag/src/PortableRendererHarness
mkdir -p artifacts/mobile-ui/gameplay artifacts/mobile-ui/presentation
GLOB2_USER_DATA_DIR="$PWD/artifacts/mobile-ui/gameplay" build/darwin/client/release/src/gameplay-touch-test
(cd artifacts/mobile-ui/presentation && GLOB2_USER_DATA_DIR="$PWD" SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software ../../../build/darwin/client/release/src/ui-presentation-test capture)
```

Substitute the host toolchain directory on Linux or Windows. The renderer harness
needs an accelerated SDL display. These checks cover build isolation, artifact
validation, rendering and input geometry; they do not replace device gameplay,
background recovery, document-picker or cross-platform simulation checks.

`mobile/ios_smoke.py` checks startup, background/resume and fresh-process relaunch
on a booted simulator named `Glob2-Mobile` in the isolated device set. Pass its
UUID with `--device`; diagnostics default to `build/mobile-smoke-ios`.
`mobile/smoke.py` and `mobile/android_trust_test.py` provide Android diagnostics;
their UI sequences may require updating as the shared interface evolves.
Keep screenshots and logs under ignored `build/` or `artifacts/` directories.
Native document picker and certificate
trust adapters are selected only for mobile builds. Simulation, replay and save
compatibility must be checked with the shared replay-verification workflow before
shipping a mobile client.

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


### Frontend layout and interaction policy

`Presentation` (`libgag/include/ui/Presentation.h`) owns frontend device
classification: the width class is Compact below 600 points and Expanded from
960, `phone()` is a touch host whose short edge is below 600 points, and
keyboard occlusion reduces the `dialog` rectangle without changing
classification. This policy is separate from the compact gameplay HUD policy
and preserves keyboard settings on narrow desktop windows.

Every menu builds one element tree; the framework stacks form fields, folds
action rows into the scrolling body and wraps action grids on narrow viewports.
The measured geometry controls drawing, clipping and hit testing. Dialog text
is distinct from interactive fields; the Enter-bound action receives primary
emphasis rather than the first button in layout order (which may be Delete).
Settings scrolls its heading, category selector, fields, save status and
actions on phones; a separate Back control commits pending text through the
existing save path. Category IDs and building-default slots remain stable
across presentations.

Custom setup keeps its draft in `CustomGameScreen` across its Map, Players and
Rules tabs on every size; there are no separate phone subpages. Launch is
disabled until the preview represents the current validated revision; keyboard
launch follows the same guard. Additional Game Options remains
multiplayer-only.

The gameplay touch harness also exercises German and Japanese inspectors and
editor actions in small portrait and landscape views, including constrained
keyboard layouts. Its localized captures stay in the isolated test profile. The
UI presentation harness checks every screen and dialog at phone, tablet and
desktop viewports, with platform gutters, in touch and pointer presentations:
interactive elements inside the safe rectangle, minimum touch targets, no
overlapping controls, unique keys and valid focus after resize. The menu colony
harness's `navigation` mode drives every menu through the screen stack and back
out with Escape. Run these alongside the gameplay-touch, settings and
custom-setup harnesses after shared frontend changes. Preserve the previous
gallery directory as the visual baseline; feedback keys must never be
renumbered.

The capture run also creates `checkpoints/foundation/`, `checkpoints/settings/`
and `checkpoints/setup/` indexes using the same images and feedback IDs. The
single-player Setup checkpoint excludes multiplayer Additional Game Options.
Missing runtime translation keys fail the capture run. Register new keys in
`data/texts.keys.txt`, provide English fallback text, and keep every language
table structurally complete; run `python3 data/check_translations.py` as well.
Languages with pending translations remain marked incomplete and use the runtime
English fallback. Frontend touch text uses separate 16/14-point font aliases so
changes to menu readability do not alter in-game/editor metrics.
