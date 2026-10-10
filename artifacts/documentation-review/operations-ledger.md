# Operations documentation migration

| Destination | Origin | Disposition | Source review |
| --- | --- | --- | --- |
| docs/multiplayer/architecture.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/contracts.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/engine-agents.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/building-library.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/colony-skins.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/platform-development.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/map-studio.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/ai-library.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/coding-studios.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/music-library.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/content-libraries.md | docs/multiplayer/architecture.md | retained and organized | platform/apps; platform/packages; src/online; deploy |
| docs/multiplayer/architecture.md#delivery-milestones | docs/multiplayer/architecture.md | removed completed implementation plan | platform/apps/web/src/App.tsx; current platform workspace |
| docs/hosting/quickstart.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/stack.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/networking.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/configuration.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/security.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/content-validation.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/scaling.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/backup-restore.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/upgrades.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/operations.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/google-cloud.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; deploy/Caddyfile; deploy scripts; platform/apps/worker/src/maintenance.ts |
| docs/hosting/limits.md | docs/hosting/README.md | retained and organized | platform/packages/core/src/blobStore.ts; src/app/cli/Headless.cpp; deploy/engine-agent-entrypoint.sh |
| docs/hosting/map-studio.md | docs/hosting/README.md | retained and organized | platform/packages/map-studio; deploy/compose.yaml |
| docs/hosting/music.md | docs/hosting/README.md | retained and organized | deploy/compose.yaml; platform/packages/music-studio; tools/music |
| docs/hosting/art-studios.md | docs/hosting/README.md | retained and organized | platform/packages/building-studio; platform/apps/api/src/terrain/studio.ts |
| docs/hosting/admin-reporting.md | docs/hosting/README.md | retained and organized | platform/apps/api/src/admin/analytics.ts; platform/packages/billing/src/reporting.ts; platform/packages/core/src/analytics.ts |
| docs/hosting/generator-studio.md | docs/hosting/README.md | retained and organized | platform/apps/api/src/generator-studio; deploy/Caddyfile; deploy/install-web-client.py |
| docs/hosting/README.md#the-former-yog-lobby | docs/hosting/README.md | removed duplicate cutover narrative | deploy/README.md |
| docs/mobile/interface.md | docs/mobile/development.md | retained and organized | src/hud/touch; src/ui; mobile/InterfacePresentation.h |
| docs/mobile/toolchains.md | docs/mobile/development.md | retained and organized | mobile/; scons/; tools/dev_build.py |
| docs/mobile/android.md | docs/mobile/development.md | retained and organized | mobile/build.py; mobile/smoke.py; .github/workflows/mobile.yml |
| docs/releases/amazon-appstore.md | docs/mobile/development.md | retained and organized | mobile/amazon_store_release.py; .github/workflows/amazon-appstore.yml |
| docs/releases/google-play.md | docs/mobile/development.md | retained and organized | mobile/play_release.py; .github/workflows/android-play-internal.yml |
| docs/mobile/ios.md | docs/mobile/development.md | retained and organized | mobile/build.py; mobile/ios_smoke.py; .github/workflows/app-signing-fingerprints.yml |
| docs/releases/testflight.md | docs/mobile/development.md | retained and organized | mobile/ios_store_release.py; .github/workflows/ios-testflight.yml |
| docs/mobile/verification.md | docs/mobile/development.md | retained and organized | tools/mobile_gallery; mobile/ios_smoke.py; mobile/android_trust_test.py; test/registry |
| docs/releases/releasing.md | docs/development/releasing.md | retained and organized | .github/workflows; tools/release; packaging scripts |
| docs/releases/gog-release.md | docs/development/gog-release.md | retained and organized | .github/workflows; tools/release; packaging scripts |
| docs/releases/mac-app-store.md | docs/development/mac-app-store.md | retained and organized | .github/workflows; tools/release; packaging scripts |
| docs/releases/china-release.md | docs/development/china-release.md | retained and organized | .github/workflows; tools/release; packaging scripts |
| docs/releases/ios-app-store.md | docs/mobile/app-store.md | retained and organized | mobile/icons.py; mobile/ios_store_release.py |
| docs/releases/windows-store.md | docs/development/reference.md | retained and organized | .github/workflows/windows-store-release.yml; .github/workflows/windows-store-auth-check.yml; .github/scripts/windows_store_release.py |
| docs/releases/desktop.md | docs/releases/releasing.md | retained and organized | .github/workflows; tools/release; mobile/ios_store_release.py |
| docs/releases/browser.md | docs/releases/releasing.md | retained and organized | .github/workflows; tools/release; mobile/ios_store_release.py |
| docs/releases/store-setup.md | docs/releases/releasing.md | retained and organized | .github/workflows; tools/release; mobile/ios_store_release.py |
| docs/releases/fdroid.md | docs/releases/releasing.md | retained and organized | .github/workflows; tools/release; mobile/ios_store_release.py |
| docs/releases/downloads.md | docs/releases/releasing.md | retained and organized | .github/workflows; tools/release; mobile/ios_store_release.py |
| docs/releases/ios-production.md | docs/releases/releasing.md | retained and organized | .github/workflows; tools/release; mobile/ios_store_release.py |
| docs/releases/releasing.md | docs/releases/releasing.md | retained and organized | .github/workflows/github-release.yml; tools/release |
| docs/multiplayer/turn-protocol.md | docs/multiplayer/turn-protocol.md | retained and organized | src/net/turn; src/relay; src/game/SimRevision.h; src/app/Version.h; src/online/SimVersion.cpp; test/fixtures/protocol |
| docs/multiplayer/turn-wire.md | docs/multiplayer/turn-protocol.md | retained and organized | src/net/turn; src/relay; src/game/SimRevision.h; src/app/Version.h; src/online/SimVersion.cpp; test/fixtures/protocol |
| docs/multiplayer/turn-timing.md | docs/multiplayer/turn-protocol.md | retained and organized | src/net/turn; src/relay; src/game/SimRevision.h; src/app/Version.h; src/online/SimVersion.cpp; test/fixtures/protocol |
| docs/multiplayer/turn-engine.md | docs/multiplayer/turn-protocol.md | retained and organized | src/net/turn; src/relay; src/game/SimRevision.h; src/app/Version.h; src/online/SimVersion.cpp; test/fixtures/protocol |
| docs/multiplayer/turn-verification.md | docs/multiplayer/turn-protocol.md | retained and organized | src/net/turn; src/relay; src/game/SimRevision.h; src/app/Version.h; src/online/SimVersion.cpp; test/fixtures/protocol |
| docs/multiplayer/account-management.md | docs/multiplayer/identity.md | retained and organized | platform/apps/api/src/auth/accountExport.ts; platform/apps/api/src/auth; platform/apps/worker/src/maintenance.ts; platform/apps/web/src/admin |
| docs/multiplayer/identity.md | docs/multiplayer/identity.md | retained and organized | platform/apps/api/src/auth; platform/packages/core/src/instanceConfig.ts; platform/apps/api/test |
| docs/multiplayer/client-content.md | docs/multiplayer/client.md | retained and organized | src/online; src/audio; src/map/generator |
| docs/multiplayer/client.md | docs/multiplayer/client.md | retained and organized | src/online/PlatformClient.cpp; src/online/screens; src/online/SimVersion.cpp |

## Additional source review

- Corrected CLI availability against `src/app/cli/Headless.cpp`, engine probe/fallback contracts against `platform/packages/engine/src/engineCli.ts` and agent configuration against `platform/apps/engine-agent/src/main.ts`.
- Verified local-only JavaScript AI publication against `src/online/MatchSetup.cpp` online rejection and the AI library schema/validation pipeline.
- Reviewed stack, health, volumes and profiles against `deploy/compose.yaml`, public isolation against `deploy/Caddyfile`, backup/restore/update procedures against their deployment scripts.
- Corrected CI-default wording to match `AGENTS.md`; validation instructions are retained as commands rather than assertions of completed verification.
- Removed provider-price estimates and certification-status assertions that cannot be established from this checkout.
- China approval reference checked against the linked NPPA imported-game application page; publisher/operator requirements and the stated 80-working-day period agree.
- Privacy policy paths and content preserved. Store listing copy remains maintained release material and must be compared with the candidate submitted.
- Added troubleshooting task routing grounded in existing health and recovery interfaces; no runtime behavior changed.
| docs/mobile/interface.md | docs/mobile/interface.md | retained and organized | src/ui/InterfacePresentation.h; src/ui; libgag/include/ui |
| docs/mobile/gameplay-controls.md | docs/mobile/interface.md | retained and organized | src/hud/touch/GameGUITouch.cpp; src/hud/touch/GameGUITouchActions.cpp; src/hud/touch/GameGUITouchDial.cpp |
| docs/mobile/editor-interface.md | docs/mobile/interface.md | retained and organized | src/map/editor; src/ui |

- Release toolchains cross-checked against `mobile/toolchain.json` (API 24/36, Xcode 27), release triggers/owner guards against Mac/GOG/Windows/iOS workflows. Corrected Mac App Store prose claiming a PR build that the manual-only workflow no longer triggers, and replaced GOG mirror-policy duplication with the canonical mirror guide.
- Backup/rollback semantics reviewed against `deploy/update-host.sh` and `deploy/restore-backup.sh`: rollback does not automatically revert the database; restoration targets a new database and reapplies known account deletions.
- Split responsive presentation, gameplay control and editor-interface contracts to remove the remaining mobile catch-all.

- Identity verification section now describes reproducible test coverage and required real-provider sign-in checks, replacing stale assertions about past testing. Relay deployment support references published image targets rather than historical compiler-run claims.

## Final validation and reader journey

- `git diff --check -- docs/multiplayer docs/hosting docs/mobile docs/releases deploy/README.md platform/README.md` passed.
- Byte changes to both public mobile privacy policies: none.
- Parsed links/anchors/headings with the new documentation checker and checked the operator journey from root index to hosting, quickstart, backup/restore and release index. Every owned-page local target/anchor is valid after the global migration; remaining library errors were in terrain/assets pages owned by the other agent.
- Inspected origin/master's online-library change and retained its new Content library patterns subsection. Cross-checked shared component names, debounced-search interval and grid minimum against its `components/library.tsx` and `styles/library.css`.
- Script options reviewed statically against the Android, iOS, app submission and deployment-smoke parsers. No deployment, secret rotation, account operation, publishing, live sign-in or real-device test was executed in this documentation change. External legal reference was checked only for the retained NPPA application statements; broader real-service and store state remains operational verification, not a claim made by this audit.
- No unresolved source contradiction was found in the inspected owned-page claims. Source checks are targeted; this ledger does not assert exhaustive proof of every historical operational claim.

## Final substantive prose review

- Replaced protocol and LAN before/after performance tables with benchmark methodology and evidence requirements, preserving source-backed regression bounds in verification. Removed the relay's historical one-machine throughput result in favor of its incremental-arbitration test and measurement inputs.
- Cross-checked 30 ticks/s against `TurnProtocol.h`, `GameTiming` and MatchSetup's `GAME_TICKS_PER_SECOND`; corrected old 25/s time conversions for bundle wakes, order rate, 250-tick queue/mutual-leave limits, catch-up/rate-control and sudden death. Explicit 25-tick scheduling intervals remain tick counts.
- Source-traced the distinct lag gates: `TurnSequencer::Config::lagThresholdTicks=50`; `TurnSequencer.cpp` marks Lagging when greater; `TurnMatchPresenter.cpp` maps Lagging to Slow; `ConnectionOverlay.cpp` forces Poor. Numeric Behind Poor threshold is independently 2,000 ms in `ConnectionQuality.h`/protocol table. Docs now explain the override rather than asserting both boundaries equal two seconds. The existing source comment claiming Slow begins at two seconds remains a code-comment discrepancy for lead review; simulation was not changed.
- Replaced matchmaking workstream/future-contract narration with current protocol/schema integration; resolved cross-page 'above' references, removed live association-file status claims, expanded long provider/signing-fingerprint tables and simplified client HUD/results/hub prose around canonical references.
- Consolidated shared mirror-hardening instructions into the release mirror guide; individual store runbooks link there. Backup examples now use recorded archive dates and a neutral restore-check database, preserving destructive/recovery semantics.
- Final checker and whitespace validation are repeated by lead after integration; policy text remains unchanged. No production-service, device, store-provider or hosted performance verification was executed.
