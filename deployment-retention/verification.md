# Deployment backup retention verification

Tested revision: 4672c0d4d1af079287d0726a4b00528ee5567191; base b602ee96896aa8d8b5fda16ca297851f8166827c. macOS 26.6.2 arm64, Python 3.14.7, Apple Git 2.54.0. Master fetched before final validation; no newer changes.

```sh
PYTHONPATH=test/deployment python3 -m unittest test_update_host test_backup_scripts test_online_deploy -v
sh -n deploy/update-host.sh
artifacts/building-studio/depot-e2e/docs-check-venv/bin/python tools/check_docs.py
```

35 tests ran: 34 passed, one util-linux flock test skipped because macOS lacks flock (the deployment host has it). The real deployment scripts run against temporary git repositories and fake Docker/gcloud, without modifying real services. Covers manual/scheduled/unmarked backup preservation, current backup after clock regression, existing timestamp retention, backup/build/swap ordering, build failures and rollback, scheduled backup tiers, live-DB restore rejection, remote deploy locking/argument boundaries and ephemeral SSH key handling. Shell syntax and documentation checks passed (307 documents, 0 errors, 74 links).

Real live-host deployment attempt exposed the deleted-current-directory bug before changing running services. Live host is Debian 12 x86_64 with Python 3.11.2 and Docker Compose 5.5.1. Final live deployment will provide real service validation; no native, browser or simulation changes are made by this PR. Native recompilation and cross-platform simulation checks are therefore not applicable to this script-only fix.

Linux follow-up: the same 35 cases all passed on the Debian 12 x86_64 live host (Python 3.11.2), including real util-linux flock. Tested clean deployment checkout d8f6eba0399e14556aa22325edce6e20a54bf7ec; tests used scratch repositories and fake service commands. Exact command: `cd /opt/glob2/src && git rev-parse HEAD && PYTHONPATH=test/deployment python3 -m unittest test_update_host test_backup_scripts test_online_deploy -v`. The release deployment created and retained its database and web-client backups, recorded the pinned design-system revision, and entered the build phase. Service-swap/browser outcome is still pending.
