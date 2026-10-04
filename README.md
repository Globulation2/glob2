# Evidence: live search counts, room previews, room visibility

Tested commit e8751773c on base c3c7e1897 (Linux x86_64, g++ 15.2.0, Node 22.22.1, Postgres 16).

- logs/platform-check.log: npm run check in platform/ (lint, typecheck, 442 passed, 6 skipped).
- logs/client-ui-online.log: UIPresentation (all sizes), Online*, PlatformProtocol*, QuickMatch*, MapCatalog* (21 passed, 0 failed).
- screenshots/: hub Play and Rooms at desktop 1280x800 and phone 390x844 (fixture with 7 online, 1 searching).
