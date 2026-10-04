# Evidence: online hub sections

Tested commit 196e21a9d on base 931fca466 (Linux x86_64, g++ 15.2.0, release=1 server=0, pinned SDL3 prefix).

- screenshots/: mobile-gallery harness at desktop 1280x800, tablet 768x1024 and phone 390x844 (Play, Rooms, Leaderboard, searching, offline).
- logs/full-suite.log: full run on an earlier revision (698 passed, 5 failed: two UIPresentation layout cases from this change, fixed since, and the 3 environment failures that also fail on master).
- logs/ui-online-final.log: UIPresentation (all sizes), OnlineWording, OnlineScreenLifetime and OnlinePlay on the final commit (13 passed, 0 failed).
