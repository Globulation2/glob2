# Evidence: online search strip and empty states

Tested commit e30da47f1 on base 1ae49fcdf (Linux x86_64, g++ 15.2.0, release=1 server=0, pinned SDL3 prefix).

- screenshots/: mobile-gallery harness at desktop 1280x800 and phone 390x844 (hub while searching, hub, empty map catalog).
- logs/full-suite.log: full run before the phone strip fix (695 passed, 5 failed: two small-phone UIPresentation cases from this change, fixed in the final commit, and the 3 environment failures that also fail on master).
- logs/ui-presentation-after-fix.log: UIPresentation, OnlineWording, OnlineScreenLifetime on the final commit (13 passed, 0 failed).
