# Final operator and release reader journeys

Static review of the final guides, starting at `docs/README.md`. Commands below describe review coverage; no deployment, provider account or publication commands were executed.

## Local instance

Actual navigation: `docs/README.md` → `docs/hosting/README.md` → `docs/hosting/quickstart.md` → `docs/hosting/security.md#sign-in-providers` → `docs/hosting/operations.md#testing-a-deployment`.

The local guide specifies clone, `glob2/deploy` working directory, example env/config copies, a generated database password, provider removal/configuration, optional served browser client, sim version, build/start, local CA trust and non-guest administrator grant. The optional browser-client command previously used a repository-root path after entering deploy; resolved to `./build-web-client.sh`. Operations now specifies repository-root Python commands and an explicit Compose file for the root-invoked match probe. Disposable versus attached checks are described. First-time Google credentials remain a reader prerequisite, not something example secrets satisfy.

## Public production configuration

Actual navigation: `docs/README.md` → `docs/hosting/README.md` → `docs/hosting/quickstart.md` → `docs/hosting/networking.md#dns-and-tls` → `docs/hosting/configuration.md` → `docs/hosting/security.md#sign-in-providers` and `#signing-keys-and-rotation`.

DNS, public origin, listener ports, proxy behavior and authentication redirect origins have discoverable instructions. Provider helper invocation now explicitly starts at repository root; rotation procedures explicitly use deploy. Configuration is separate from provider-specific credentials. Public TLS and provider registrations require operator-owned domain/accounts. Security/key backup contracts are retained.

## Backup and recovery

Actual navigation: `docs/README.md` → `docs/hosting/README.md` → `docs/hosting/backup-restore.md#backups-and-restore` → `#scheduled-backups` → `docs/hosting/operations.md#testing-a-deployment`.

The manual procedure archives database, blobs and signing/service/database keys; secrets archive encryption and isolated restoration are explicit. Scheduled GCS backups clearly cover only the database and deletion manifest. The documented restore helper restores a separate named database, applies migrations and re-applies recorded account deletions; the irrecoverable deletion gap when both the live database and latest deletion record are missing remains explicit. Root-versus-deploy transitions for scheduled scripts and Compose cutover are now explicit. Source inspection: `deploy/backup-to-gcs.sh`, `deploy/restore-backup.sh`, `deploy/gcs-backup-lifecycle.json`, Compose/init behavior reviewed during the operations ledger pass. No restore drill was performed.

## Upgrade and rollback

Actual navigation: `docs/README.md` → `docs/hosting/README.md` → `docs/hosting/upgrades.md#upgrades` → `docs/hosting/backup-restore.md#backups-and-restore`; additional branches `#draining-relays`, `#sim-versions-and-engine-agents`, `#images`, `#automatic-deployment`.

Manual Compose upgrades now state deploy cwd. The host helper and worktree/image builds explicitly state repository-root cwd; role and relay procedures state deploy cwd. The helper archives the database and web client, retains previous images/revision, switches images before web client and rolls back unhealthy image deployment. Database rollback remains manual; migration compatibility and retained old simulation agents are explained. Source inspection of `deploy/update-host.sh` confirms revision recording, previous image tags, database dump, rollback branch and separate web install. No live upgrade or failure-injection exercise was performed.

## Prepare and publish a release

Actual navigation: `docs/README.md` → `docs/releases/README.md` → `docs/releases/downloads.md#release-qualification` and `#qualified-downloads-manifest` → `docs/releases/releasing.md#the-release-mirror` → `docs/releases/desktop.md`, `browser.md`, `store-setup.md` or the relevant store guide.

Qualification distinguishes builds from installed-package/device/gameplay evidence, save/replay/network and simulation compatibility, backed-up signing keys, public production store availability and exact source/artifact digests. Mirror policy is centralized. Source inspection of `.github/workflows/github-release.yml`, `.github/workflows/promote-downloads.yml` and `tools/release/downloads_manifest.py` confirms mirror repository/ID guards, reviewed qualification URL/digest inputs, manifest generation and final draft promotion. No release dispatch, signing, provider console, store submission or remote manifest/public listing verification was performed.

## Validation and remaining limits

`PYTHONPATH=artifacts/docs-runtime python3 tools/check_docs.py` passed after the cwd edits: 304 Markdown documents, 0 errors, 67 external links. This checks local Markdown links/anchors and document structure, not command execution or external URL reachability. The working-directory blockers found in this review are resolved. No remaining blocker was found in these five static journeys. The earlier operations ledger records the scope of selected source checks; this review does not certify every factual claim or the operational readiness of a deployed host/release account. Privacy policy files were not edited.
