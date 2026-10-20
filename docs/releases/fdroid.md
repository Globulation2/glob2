# F-Droid distribution

Maintain F-Droid source builds and independent distribution metadata.

## Distribution maintainers

### F-Droid Android release

The main F-Droid repository builds and signs `org.globulation2.glob2` from a
public `vVERSION` tag. This is the same application ID used by Play. The
`--fdroid` build mode applies F-Droid's version codes and unsigned APK checks.
The upstream build recipe is `fdroid/metadata/org.globulation2.glob2.yml`;
submit its tested contents to F-Droid's `fdroiddata` project. The recipe removes compressed CI
prediction data from the build checkout before source scanning; these files
are not Android build inputs.
`mobile/android-release.json` gives the version
name and base code, and the `armeabi-v7a`, `arm64-v8a`, and `x86_64` APKs use
`10 * base + 1`, `+ 2`, and `+ 3`, respectively. The `fastlane/metadata/android/`
tree contains the listing text, icon, per-APK changelogs, and Android screenshots.
Release preflight checks all three recipe codes and rejects missing listing
material. Do not use a
desktop or browser screenshot as an Android screenshot.

The mobile and build-only release workflows retain unsigned APKs as validation
artifacts; those files are not installable public releases. A temporary
developer signature supports emulator and device checks. The all-platform
staging workflow signs those verified unsigned APKs in its protected
`android-sideload` job using `mobile/sideload_release.py`, verifies the pinned
permanent signing certificate and alignment, and stages only the final signed
APKs. APK verification checks every packaged native ELF's ABI and load-segment
alignment: 16 KB pages for ARM64 and x86-64, and 4 KB pages for ARMv7. This is
separate from ZIP alignment of the final APK. F-Droid's signature remains the
sole public F-Droid update channel.
The reviewed `.github/workflows/fdroid-release-validation.yml` workflow is
mirrored to `genixpro/glob2-release` and runs by owner dispatch with
the exact public candidate commit SHA. Its read-only jobs build all three
unsigned APKs through
`mobile/fdroid_build.py`, retain checksums and logs, and launch the x86_64
candidate in an emulator. After approval and tagging, dispatch it again with
the public tag to verify the tag points to that same source commit. Compare its
APKs with the public workflow artifacts before submitting the recipe.
Play and F-Droid use different signing keys, so Android cannot update an
installation from one store with an APK from the other. A store switch requires
uninstalling the installed copy; export or back up saves first.
Before tagging, run `fdroid lint` and exercise `fdroid checkupdates` with a
disposable tag fixture; the update checker cannot discover an unpublished
public tag. Confirm it generates all three ABI version codes.
Dispatch `F-Droid buildserver recipe trial` on `genixpro/glob2-release`
for each ABI with the exact public candidate commit SHA. It runs the recipe with
`fdroid build --on-server` inside F-Droid's pinned buildserver image. The trial
uses a disposable copy of the reviewed recipe, updating all three version names,
ABI codes, and current-version fields from the candidate manifest and pointing
each `commit` field to that SHA. This supports candidates newer than the reviewed
template. The public tag does not exist until these gates pass. The source build
installs the repository's checksum-pinned JDK so its Java bytecode can be
compared with GitHub's APK. Retain the trial APKs and logs. Submit the materialized
recipe to `fdroiddata` with full commit hashes that match the final public tag,
as required by its app inclusion template. Play the candidate on
real ARM64 and 32-bit ARM devices and an x86_64 emulator. Record APK digests,
logs, screenshots, save/load,
rotation, lifecycle, keyboard, touch, and editor results under `artifacts/` for
review. For each ABI, run `python3 mobile/compare_fdroid_apks.py --arch ABI
--github-apk GITHUB_APK --fdroid-apk FDROID_APK` and review any native library
differences against the retained symbol/build records. The daily
`fdroid-publication.yml` workflow in the release mirror checks F-Droid's package API
for all three codes after the GitHub release and opens one tracking issue when
publication remains incomplete after 72 hours.

F-Droid's initial listing requires an accepted `fdroiddata` merge request.
Run `fdroid rewritemeta org.globulation2.glob2` on the materialized submission
recipe as well as `fdroid lint`. Commit its canonical field order and line
wrapping. Run `fdroid checkupdates` against the current public tags and refresh
the submission to the selected qualified candidate if newer tags have appeared;
a successful build does not make a stale submission's update check pass.
Fork `fdroid/fdroiddata` into the submitting maintainer's personal GitLab
namespace; a new GitLab group is unnecessary. Add only the app's YAML file on a
dedicated branch and open a draft merge request using the app inclusion template.
Keep release validation and license questions explicit until resolved; a draft
submission does not establish release readiness. If GitLab account verification
blocks fork CI, note that in the merge request so F-Droid maintainers can run it.
Subsequent tagged updates use F-Droid's update checker; GitHub Actions does not
publish directly to the main F-Droid repository. Review the shipped asset
licenses against `docs/assets/source-attribution.md` and the font licenses with
the first submission.

The Fedora spec in `fedora/glob2.spec` is a starting recipe for the new source
archive. The checked-in `debian/` directory is historic and is **not** used by
this workflow; work with the Debian Games Team's Salsa package before proposing
an update. Debian's upload can subsequently reach Ubuntu during its development
cycle. Arch and openSUSE have their own maintainers and package repositories.
Send each maintainer the source archive, checksum, dependency changes, release
notes, build results, and any save/replay/network compatibility notes. Official
repository uploads require their review and cannot be performed by our GitHub
token.

[Release index](README.md) · [Documentation index](../README.md).
