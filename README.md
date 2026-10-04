# Evidence: one search in several queues

Tested commit 542c17ffb on base a2c3cc045 (Linux x86_64, g++ 15.2.0, Node 22.22.1, Postgres 16).

- logs/platform-check.log: npm run check in platform/ (lint, typecheck, 448 passed, 6 skipped), including the new matchmaker cases (hold while prompted, resume in place, decline ends the search, leave/update the whole search, one search per account) and the API queue.join queueIds case.
- logs/client-ui-online.log: UIPresentation (all sizes), Online*, PlatformProtocol*, QuickMatch* (with the multi-queue search case), MapCatalog*: 21 passed, 0 failed.
- screenshots/: Play for a registered player with Also search, at desktop 1280x800 and phone 390x844.
