# TestFlight releases

Archive, sign and upload the iOS app for beta testing.

## TestFlight upload

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

App Store Connect's App Privacy answers and privacy policy URL follow the same
[privacy policy](../mobile/privacy-policy.md) as [Play's Data safety form](google-play.md#google-play-internal-testing). Universal
links for invites need the Associated Domains capability on the App ID and the
official instance's `apple-app-site-association` file
([mobile app links](../hosting/networking.md#mobile-app-links)).

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
Prepare the public product-page copy, screenshots, privacy answers and submission
checks with the [App Store page guide](ios-app-store.md).

[Release index](README.md) · [Documentation index](../README.md).
