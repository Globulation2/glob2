# PR 830 reference regeneration evidence

Tested fixture head: 02ca50cc66451162d11a243a6d06c2c0b2c90def; base and fetched master:0329651ec1e4b6fa63f7e2fa45c09dd4ddfcd933, unchanged. Changes are only five current checksum references and fixture documentation. Retained legacy save inputs, historical reference files, all assertions, test scripts and engine computation are unchanged. Simulation revision20 was already bumped by805 and its multiplayer golden regenerated; this reference-only repair needs no further bump.

The earlier 805 integration expands Building::checkSum slot16 with production/construction/repair/reservation fields, and Unit::checkSum with service/construction/carried-packet state. The old terrain-era references diverge at tick0; first-tick-checksum-differences.json records exact entities/field indices. The same recorded master binary reproduces the canonical hosted mismatch (SHA6306cf7456d7754bc2587883749e5589821abf79ca84c71f347b26236c8bc920), and the retained Maxima trajectory diverges at its first tick30000. This is reference drift after the intentional checksum expansion, not evidence of unreadable legacy saves. Independent midpoint save/reload comparisons passed before the Maxima expectation was regenerated.

## Binaries and environments

Unmodified released CI binaries for source5f090cbefb569f501d3709cd1389773c0a59e545:
- GCC13 Ubuntu24 artifact https://github.com/Globulation2/glob2/actions/runs/37442302378/artifacts/11403028929 ; executed on Ubuntu26.04.1 x86_64 host with its bundled patched SDL libraries through absolute LD_LIBRARY_PATH.
- GCC11 Ubuntu22 artifact https://github.com/Globulation2/glob2/actions/runs/37442302378/artifacts/11403341008 ; executed inside Docker ubuntu:22.04 with the artifact runtime-packages.txt installed and its own bundled patched SDL libraries. Root checkout/git metadata read-only, evidence directory writable, container removed after exit. Docker image id/digest recorded separately. Host-only GCC11 attempts failed loading libtiff.so.5 and are retained; only Ubuntu22 container runs are accepted.

Archive provenance/runtime-package files and binary hashes are retained. These binaries predate829, whose constexpr repair changes only constant storage, and830, which changes only fixtures/docs. No engine computation/source inputs affecting these trajectories differ from the tested binaries. GCC13 terrain runs were made at43566c1f5; those four fixtures/code remain byte-identical at final02ca50cc. All final Maxima and GCC11 checks were made against final02ca50cc. No binary is claimed rebuilt from830.

## Exact commands and results

ROOT is the absolute checkout; PREFIX is the corresponding gcc13-hosted or gcc11-hosted unpacked build/sdl3-ci/prefix/lib. Each generated scenario includes exact subprocess command.json and run.log. Initial baseline runs retain failures, regeneration retains manifests; no fixture writes occur in final verification.

```sh
gh run download 37442302378 --repo Globulation2/glob2 --name linux-test-programs-ubuntu-24.04 --dir artifacts/capability-telemetry-repair/gcc13-hosted
gh run download 37442302378 --repo Globulation2/glob2 --name linux-test-programs-ubuntu-22.04 --dir artifacts/capability-telemetry-repair/gcc11-hosted
# Extract linux-test-programs.tar.gz into each artifact's unpacked/.
LD_LIBRARY_PATH=$PREFIX python3 test/check_telemetry_simulation.py $BINARY --update-fixtures --output artifacts/capability-telemetry-repair/regenerated-gcc13
LD_LIBRARY_PATH=$PREFIX python3 test/maxima/check_save_continuation_fixture.py $BINARY --update-fixtures --output artifacts/capability-telemetry-repair/maxima-regenerated-gcc13
LD_LIBRARY_PATH=$PREFIX python3 test/check_telemetry_simulation.py $BINARY --output artifacts/capability-telemetry-repair/final-COMPILER-MODE
LD_LIBRARY_PATH=$PREFIX python3 test/check_telemetry_simulation.py $BINARY --parallel-ai --output artifacts/capability-telemetry-repair/final-COMPILER-parallel
LD_LIBRARY_PATH=$PREFIX python3 test/maxima/check_save_continuation_fixture.py $BINARY --output artifacts/capability-telemetry-repair/final-maxima-COMPILER-serial
LD_LIBRARY_PATH=$PREFIX python3 test/maxima/check_save_continuation_fixture.py $BINARY --parallel-ai --output artifacts/capability-telemetry-repair/final-maxima-COMPILER-parallel
```

GCC11 container preparation: apt-get update; xargs apt-get install -y --no-install-recommends python3 git < gcc11-hosted/runtime-packages.txt; git safe.directory for the read-only checkout; set absolute LD_LIBRARY_PATH, then execute the four final commands. gcc11-final-container.log retains all preparation/results. Complete evidence output directories distinguish initial/final and serial/parallel runs. All accepted final commands exit0.

All four legacy scenarios passed for both compilers in both worker modes:1024+2048+2048+256=5376 complete records per combination. Maxima retained v115 checkpoint passed all512 baseline complete hashes, then all256 independent midpoint reload records, for both compilers/modes. Only the established MapHeader version contribution is adjusted during Maxima midpoint comparisons; all entity bytes and every other aggregate bit remain asserted. cross-toolchain-checksum-summary.json independently verifies exact full-sidecar bytes across toolchains/modes (legacy v108 sidecars also include aggregate). Generated replays, checksum sidecars, save checkpoints, manifests and logs are included.

Failed master jobs: https://github.com/Globulation2/glob2/actions/runs/37442302378/job/112211113868 (GCC13 CLI), https://github.com/Globulation2/glob2/actions/runs/37442302378/job/112211383809 (GCC11 CLI), https://github.com/Globulation2/glob2/actions/runs/37442302378/job/112211113959 (Maxima continuation).

## Limits and acceptance

No Windows/macOS/Android/browser traces were generated locally; hosted affected checks requested. No save inputs, simulation computation, scheduling, assertions or compatibility floor were changed. Focused checks directly cover the changed baselines and loading/resuming older saves; complete native unit/engine matrix not repeated for reference-only changes. Historical version123/other policy references remain unchanged. The old master run also reports separate WSS/menu harness failures, which are preserved and will be repaired independently; this PR does not claim to fix those or the historical Firefox startup flake.

Maintainer acceptance: Codex, acting under author authorization/AGENTS.md, accepts this evidence for final02ca50cc/base0329651e. Normal branch protections retained. Full master CI must confirm these jobs recover.

Evidence archive uses tar hardlinks for byte-identical files; normal tar extraction reconstructs every evidence path. Join ordered verification.part-* files before extraction. Complete bytes are retained without duplicate storage.
