# iOS production submission

Submit a qualified iOS build while retaining manual publication.

## iOS production submission and manual publication

Dispatch `ios-testflight.yml` from mirror `master` as the owner with the exact
public tag and `audience: external`. An eligible upload emits the
`ios-upload-provenance` artifact only after upload succeeds, recording the tag,
source commit, application ID, marketing version, build number and archive binary
hash. The artifact is retained for 90 days. Save release evidence externally if
review may outlast that retention period. Internal-only uploads cannot be used
for App Store production.

Dispatch `ios-production.yml` with the same `tag`, the successful mirror
`upload_run_id`, and one deliberate `stage`: `prepare`, `submit` or `publish`.
The context job verifies the owner-dispatched mirror run, expected workflow,
success and exact source provenance before the protected `ios-testflight`
environment supplies App Store Connect credentials. Apple queries must find
exactly one processed, eligible build with that build number and marketing
version. A version attached to a different build is never overwritten.

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
python3 -m unittest discover -s test/build_system -p test_downloads_manifest.py -v
```

[Release index](README.md) · [Documentation index](../README.md).
