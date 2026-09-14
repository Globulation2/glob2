# Tournament validation — 2026-09-14

Implementation guide: [Distributed tournaments](tournaments.md). Reviewable evidence is
in [the compressed validation archive](../test/fixtures/tournaments/validation-20260914.tar.gz).
Its adjacent index records every member and SHA-256. Extract it with `tar -xzf` into
an empty directory. Binaries/data caches remain locally under
`artifacts/tournaments-validation/bundles/`; the archive includes their immutable manifests.

## Verified results

* 20 Python tests: leases, expiry/late results, duplicate suppression, restarts, lost
  acknowledgements and cleanup, resumable/corrupt chunks, disk admission, immutable
  builds, real watchdog termination/retry, PID-bounded diagnostics, Elo/ties/rosters,
  schedule pairing, all-tie fairness, and offline manifest-order reanalysis.
* Existing fairness statistics tests passed. Real header tests cover full/text/partial
  serialization, version-100 loading without consuming extension bytes, and configured
  Cortex/Maxima instances. Network partial headers round-trip; YOG protocol 29 is
  rejected and 30 accepted. Replay floor/future-version/decode-version tests passed.
* 14 production CLI cases on macOS arm64 and Linux x86-64: catalog, seeded generation,
  verified rotations, invalid configurations, independent overrides, initial/checkpoint
  save continuation, ambient environment isolation, unresolved caps, allied winners,
  and loading the version-84 game fixture. Catalog startup at a long executable path
  is checked too. Each suite records the tested executable SHA-256, matching its bundle.
* Final macOS/Linux checksum files are byte-identical for all 2,048 ticks of configured
  Cortex and Maxima games. Generated map bytes, Cortex initial saves and Maxima tick-512
  checkpoints are also identical. Reloaded initial/checkpoint traces match uninterrupted
  execution. Earlier transferred-save runs independently verified Cortex ticks 0–2047
  and Maxima ticks 512–2047. See `cross-platform-release.json` and retained raw traces.
* The original version-100 macOS binary and changed binary produce identical per-tick
  traces for 2,048 ticks from the same old initial saves: Cortex duel and four-colony
  Maxima. This checks default behavior as well as configured continuation.
* All four designs ran through real local workers: AI comparison 5 jobs, fairness 3,
  generator stress 4 (including validation rejections), ablations 5. Reanalysis passed
  under prestige, survivor-draw and military policies. Raw observations and reports are
  retained. Legacy benchmark, fairness and generator entry points also completed smoke
  runs, including generator reproducibility and terrain output.

## Six-machine reliability pilot

The final pilot passed every check: localhost plus all five Linux hosts, one slot
each, 36 logical games, 40 attempts: 36 accepted, 3 retained late duplicates, and one
interrupted attempt. No final-pilot engine crashes or watchdog failures occurred.
A worker daemon was killed on pharaoh-dev-1, an attempt process group on pharaoh-dev-3,
and the coordinator process was killed and restarted. Coordinator contact with
pharaoh-dev-2 was withheld for at least 40 seconds, exceeding the pilot lease of 30
seconds. A queued-to-running transition during that interval verifies buffered execution;
expired jobs were accepted from other hosts and late results could not replace them.
The pilot stopped its workers after collection. It changed no networking configuration
and did not reboot hosts or interrupt unrelated processes.

The final measured phase took 271.4 seconds, excluding initial package/bundle warming.
These are short Cortex runs on a two-colony, seeded symmetric map, not a capacity
benchmark. Concurrent validation and existing host workloads can affect wall-time rates.
Slots remain conservative; measure representative Maxima/FFA workloads before raising
production concurrency. Peak RSS is the maximum observed child-process high-water mark.

| Host | Slots | Accepted | Successful attempts | Peak RSS MiB | Ticks / process wall second |
| --- | ---: | ---: | ---: | ---: | ---: |
| localhost | 1 | 6 | 6 | 68.4 | 650.9 |
| therig.local | 1 | 6 | 6 | 56.9 | 247.2 |
| devlaptop.local | 1 | 9 | 9 | 53.9 | 373.8 |
| pharaoh-dev-1.local | 1 | 6 | 6 | 53.7 | 211.2 |
| pharaoh-dev-2.local | 1 | 3 | 6 | 60.1 | 210.6 |
| pharaoh-dev-3.local | 1 | 6 | 6 | 60.1 | 247.0 |

The complete inputs, job seeds (300 onward), manifests, attempts, leases, event times,
logs, stdout/stderr, host snapshots, resources and acceptance checks are in
`pilot-release/`. Host directories are recorded in `pilot-hosts.json`.

## Reproduction and build identity

The Linux client was prepared in the isolated directory
`/home/bradley/glob2-tournament-validation-20260914/source` on devlaptop using existing
dependencies and `scons -j4 release=1 server=0`. The framework itself performs no builds
or dependency installation. The macOS client uses the same source and release flags.

* Linux bundle: `4cca9054b35f5f6728fe2fedf268b18abf0bdfdf3e935e3c0b45248b052952b8`
* macOS bundle: `35bcea4289fd81ace40142399185d655e4897d33a6b4b4d940d021b62d8cb8ac`
* Dirty source identity: `6d7af58f72e9b98ba817bfd99f1be432587a8d259ad6a593d289ced687f63daf`

`source-file-hashes.json` defines the source snapshot identity. Bundle manifests capture
source revision, build options, platform, executable/data hashes and CLI capabilities.
The final experiment pins and retains its worker package as `worker.pyz`.

```sh
python3 test/test_tournaments.py
python3 test/test_map_fairness_tournament.py
scons -C test -j4 GameHeaderTextSaveLoadTest ReplayStepCounterTest
test/GameHeaderTextSaveLoadTest
test/ReplayStepCounterTest
scons -j4 release=1 server=0 tournament-compatibility-test
build/src/TournamentCompatibilityTest
python3 test/tournament_cli_integration.py --output artifacts/tournament-cli
python3 test/tournament_reliability_pilot.py --output artifacts/new-pilot \
  --hosts HOSTS_JSON --mac-bundle MAC_BUNDLE --linux-bundle LINUX_BUNDLE \
  --map RETAINED_MAP
```

## Findings and limits

The first SSH pilot exposed a pre-existing Linux startup assertion: FileManager used
a 100-byte executable-path buffer. Immutable bundle paths exceeded it. Dynamic readlink
buffering fixed it; both long-path CLI tests and the final SSH pilot pass. Failed-pilot
attempts and logs are preserved in the archive. An earlier pilot completed all 36 jobs
but its buffer assertion required two *finished* jobs; the corrected assertion checks
the observed queued-to-running transition during disconnection. Both event records remain.

A compatibility harness also caught Maxima resolving defaults before the new player
count was installed. Explicit per-player values now survive construction, while legacy
format defaults are resolved after header installation. Cortex continuation now saves
queued orders, expansion debounce and settle clocks. These are format-101 additions;
the save floor remains 58, and network/YOG protocol becomes 30.

Structured games assign AI players directly in team order. The legacy random-game
driver inserted a passive local player and polled team 0 last. Migrated benchmark
runs therefore belong to a new cohort when comparing historical scores; order can
change how a game plays. Engine tick caps remain unresolved and policies live offline.

Windows execution/checksum equivalence has not been verified here; CI retains its
Windows build coverage. The macOS/Linux traces cover these finite scenarios, not all
late-game states. Saves older than the retained version-84 fixture were not exercised;
their gated loaders and minimum-version floor remain intact. External Cortex ML model
selection remains a legacy interface; structured per-player Cortex tuning covers the
existing numeric vector. Core/stack extraction is best effort and was tested with
fixtures, not a real core dump. No claim is made that transfer hashes detect faulty RAM.

These changes have not been merged. Gameplay feel and the new driver’s ordering remain
matters for maintainer review; automated evidence does not replace that review.
