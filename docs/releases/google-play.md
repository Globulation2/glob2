# Google Play internal releases

Prepare signed Android artifacts and publish the internal testing track.

## Google Play internal testing

The Play app ID is `org.globulation2.glob2`. The Play upload is a release Android
App Bundle. Install Android SDK platform 36
alongside the pinned NDK and build tools. The [Android APK commands](../mobile/android.md) remain
useful for direct device testing. To build the bundle:

```sh
python3 mobile/android.py bundle --arch arm64-v8a --release --version-code 1
```

Each subsequent Play upload needs a higher `--version-code`. The bundle command
restores gzip assets that Android packaging expands, verifies the complete game
asset index, and checks the packaged native build ID against retained symbols.
The unsigned bundle is written to
`build/android/device/arm64-v8a/24/client/release/android-project/app/build/outputs/bundle/release/app-release.aab`.
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

Link the [privacy policy](../mobile/privacy-policy.md) in the store listing and keep Play's
Data safety form in step with it. The Play build includes online play, so the form
must declare what the official instance collects when a player goes online (account
and display name, optional e-mail address from a sign-in provider, user IDs, in-game
and room chat, uploaded maps, match and connection-quality data, IP addresses in logs
and rate limits), plus first-party online account activity (90-day identifiable
markers and 24-month anonymous daily totals). It is sent over TLS; nothing is used
for advertising or tracking across apps or sites. Players can download their data
and delete their account at
`https://app.glob2online.com/account`. Single-player, editor and LAN play collect
nothing. The game has no minimum age; the Play Console target-audience and content
answers must match the policy's [children](../mobile/privacy-policy.md#children) section. Invite links open the app only after the official instance publishes the Play
app-signing fingerprint ([mobile app links](../hosting/networking.md#mobile-app-links)).

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
git fetch origin master
git fetch upstream master
git checkout --detach upstream/master
git merge -s ours --no-edit -m "Merge public glob2 master into release mirror" origin/master
git diff --exit-code upstream/master   # the tree must equal upstream's
git push origin HEAD:master
```

The merge keeps the mirror's history (so the push is not a force push) while
taking upstream's files exactly; the release mirror never carries its own
changes. Make any release-specific change upstream first; see
[the release mirror](releasing.md#the-release-mirror). Inspect
the new commits before pushing. Do not force-push a release branch.

After syncing, run **Actions → Android Play internal release → Run workflow**
in the mirror. The optional release notes are shown to internal testers. The
run summary records the version code and signed AAB SHA-256; only Play receives
the signed bundle. Check the internal testing track in Play Console for
availability; if Play requires a new content or policy
declaration, complete that in Play Console before rerunning the workflow.

[Release index](README.md) · [Documentation index](../README.md).
