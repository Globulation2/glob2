# iOS production submission

Submit a qualified iOS build while retaining manual publication.

## iOS production submission and manual publication

Dispatch `ios-testflight.yml` from mirror `master` as the owner with the exact
public tag, `upload: true` and `audience: external`. An eligible upload emits the
`ios-upload-provenance` artifact only after upload succeeds, recording the tag,
source commit, application ID, marketing version, build number and archive binary
hash. The artifact is retained for 90 days. Save release evidence externally if
review may outlast that retention period. Internal-only uploads cannot be used
for App Store production.

Dispatch `ios-production.yml` with the same `tag`, the successful mirror
`upload_run_id`, and one deliberate `stage`: `inspect`, `supersede`, `prepare`, `submit` or `publish`.
`inspect` is read-only: it lists version IDs, version strings, states, release
types and attached build IDs to diagnose existing-draft conflicts. It also lists
app-information states and the public primary-locale privacy-policy URL to
distinguish missing metadata from a state-filter mismatch. It uses the
reviewed mirror inspector and the tagged authentication/provenance helpers; it
never logs review contacts, credentials or full Apple response bodies.
The context job verifies the owner-dispatched mirror run, expected workflow,
success and exact source provenance before the protected `ios-testflight`
environment supplies App Store Connect credentials. Apple queries must find
exactly one processed, eligible build with that build number and marketing
version, with Apple explicitly reporting `expired: false`. Missing or unknown
expiry blocks preparation, review submission and publication before any store
write. By default a version attached to a different build is never overwritten.
To advance an unsubmitted candidate draft, the owner must select `prepare`,
its `draft_version_id`, and the currently attached `replace_draft_build_id`.
Replacement is limited to an editable `PREPARE_FOR_SUBMISSION` draft with
`MANUAL` release; a changed build identity or review state blocks the write.
The exact new build must still have processed, eligible source provenance.
Retries accept an already attached selected build. This preserves the existing
listing while advancing its version and build, without submitting or publishing.
The selected public tag must include replacement support; older tagged tooling
fails before protected store credentials are obtained.

`supersede` explicitly withdraws an older manual-release iOS version that is
waiting for review, then binds the new provenance-verified eligible build and
marketing version to that same version record. Pass the inspected
`draft_version_id` and exact current `replace_draft_build_id`. It refuses changed
identities, active review, newer versions, automatic release, or a submission
containing other items. Apple's review queue position is lost on withdrawal.
Retries can resume the same withdrawn editable version; submission of the new
version remains a separate `submit` action. This driver action preserves listing
metadata and never bypasses Apple's review or qualification.

Store write stages use the reviewed mirror's production driver while the uploaded
binary, marketing version and provenance remain bound to the immutable public
tag. Privacy-policy checks read current and legacy AppInfo state fields and
include rejected editable app-information records; the primary-locale HTTPS URL
is still mandatory, and Apple independently validates review eligibility.

`prepare` creates or binds the selected version with `MANUAL` release and does
not submit it. Complete private store listing configuration before `submit`:
copyright, localized descriptions and HTTPS support links, primary-locale
privacy policy, fully uploaded iPhone and iPad screenshots, and review contact
information plus demo credentials when required. Apple validates additional
agreements, ratings and review requirements; failures remain blocking.
`submit` requests Apple review without public distribution. Only `publish`,
after Apple returns `PENDING_DEVELOPER_RELEASE` for the same manually released
version and build, requests public distribution. An accepted upload or pending
review never counts as production availability for the website launch gate.

`mobile/ios_release.py` encodes the four-component game version `a.b.c.d` as
App Store marketing version `a.b.(100*c+d)`. Each component is numeric without
leading zeroes and `d` must be below 100 to avoid collisions. Unsupported versions
fail before the iOS build; the generated Info.plist and provenance use this exact
encoding instead of a stale fixed marketing version.

Run the focused metadata contracts with:

```sh
python3 -m unittest discover -s test/build_system -p test_ios_store_release.py -v
```

[Release index](README.md) · [Documentation index](../README.md).
