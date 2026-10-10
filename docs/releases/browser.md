# Browser publication

Build and publish both browser variants.


Dispatch `browser-release.yml` in `genixpro/glob2-release` from `master` with
an existing public `vVERSION` tag. Only an owner manual dispatch (including the
initiator of a rerun) passes the context gate. This workflow is separate from
the desktop release orchestrator and has no push, pull-request or reusable
workflow trigger.

The build job verifies the exact public tag commit and matching game version
(without requiring desktop store metadata), installs the locked
Emscripten SDK, builds `scons target=web release=1`, and packages the client with
`browser/package-static.py`. Its `browser-release` artifact includes SHA-256
checksums. The isolated publication job verifies the artifact before using
Google Cloud Workload Identity Federation through the `browser-release`
GitHub environment; the build job has no cloud identity permission.

The destination is `gs://glob2-browser-pharaoh-418820-20260909` in project
`pharaoh-418820`. Versioned JavaScript, WebAssembly and data objects receive
explicit content types and immutable one-year caching. They are uploaded before
`index.html`, which requires cache revalidation. Older assets remain available
for open sessions and rollback; publication never synchronizes with deletion.
The public entry point is
<https://storage.googleapis.com/glob2-browser-pharaoh-418820-20260909/index.html>.

The release environment requires `GLOB2_BROWSER_WIF_PROVIDER` and
`GLOB2_BROWSER_SERVICE_ACCOUNT` variables. The dedicated `glob2-browser-release`
provider restricts tokens by repository ID, owner actor ID, manual event,
`master`, public visibility, hosted runner, environment and exact workflow path.
The service-account federation binding uses the provider-specific
`attribute.browser_release_repository_id/1397722696` principal set. Only this
provider maps that attribute; retain its workflow and environment restrictions
and do not map the attribute in unrelated providers. This also avoids dependence
on GitHub's mutable versus immutable subject string format.
Its service account has `roles/storage.objectUser` on this bucket only, with no
project-wide storage role or persistent service-account key. Add the pinned
`google-github-actions/setup-gcloud` reference from the workflow to the mirror's
allowed actions, preserving its existing list. Configure the environment to
allow only `master`. Mirror reviewed workflow changes before dispatching; an
implementation change alone does not publish or replace the live browser.

[Release index](README.md) · [Documentation index](../README.md).
