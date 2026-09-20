# School and upgrade policy validation

Final policy: preserve early school gates, then target max(advanced school target,
population / 50), rounded down. Upgrades preserve at least half of each category
with two or more existing buildings. A lone building may upgrade, including a
lone barracks. The existing barracks-seat safeguard still applies when multiple
barracks serve a training backlog. No new saved fields are introduced.

Focused validation on macOS arm64:

- Implementation, economy and director native regressions passed. Coverage includes
  seven upgradeable categories, populations of one through six buildings, queued
  orders, new sites, registry reload and rejection recovery; school thresholds
  at 199/200 and 250 population retain the early gates.
- Python source contracts: 61 tests passed, one skipped.
- Format-115 fixture loaded successfully. Reload at tick 30256 matched all 256
  following detailed team/entity records through tick 30512. The archive retains
  all 512 baseline state hashes, source hashes and test logs.

[Validation artifacts](validation.tar.gz). Reproduce from a release-built root:

```sh
gzip -dc test/maxima/fixtures/save-continuation/checkpoint-30000-v115.game.gz > /tmp/policy-input.game
build/src/glob2 --run-game --load-game /tmp/policy-input.game --ticks 30512 --telemetry checksums --save every:30256 --output-dir /tmp/policy-full
build/src/glob2 --run-game --load-game /tmp/policy-full/checkpoint-30256.game --ticks 30512 --telemetry checksums --output-dir /tmp/policy-resume
python3 test/compare_save_continuation.py /tmp/policy-full/game.replay.checksums /tmp/policy-resume/game.replay.checksums
```

Use fresh output directories. The final one-per-50 combined policy has not had a
broad gameplay sweep or cross-platform checksum verification. Earlier experiments
used 40/60/90 without the guard and do not establish this policy's balance.
Manual gameplay review remains outstanding. Existing upgrades are allowed to
finish even if a loaded save already exceeds the limit. This changes Maxima's
construction/training pacing and remains part of the unmerged policy work.
